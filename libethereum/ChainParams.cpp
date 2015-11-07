/*
	This file is part of cpp-ethereum.

	cpp-ethereum is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	cpp-ethereum is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with cpp-ethereum.  If not, see <http://www.gnu.org/licenses/>.
*/
/** @file ChainParams.h
 * @author Gav Wood <i@gavwood.com>
 * @date 2015
 */

#include "ChainParams.h"
#include <json_spirit/JsonSpiritHeaders.h>
#include <libdevcore/Log.h>
#include <libdevcore/TrieDB.h>
#include <libethcore/Sealer.h>
#include <libethcore/BlockInfo.h>
#include "GenesisInfo.h"
#include "State.h"
#include "Account.h"
using namespace std;
using namespace dev;
using namespace eth;
namespace js = json_spirit;

ChainParams::ChainParams(Network _n):
	ChainParams(genesisInfo(_n))
{
	stateRoot = genesisStateRoot(_n);
}

ChainParams::ChainParams(Network _n, bytes const& _genesisRLP, AccountMap const& _state): ChainParams(_n)
{
	if (!RLP(&_genesisRLP)[0].isList())
	{
		cwarn << "*************************************************************";
		cwarn << "**                                                         **";
		cwarn << "**  BAD GENESIS BLOCK PASSED - SHOULD BE BLOCK NOT HEADER  **";
		cwarn << "**                                                         **";
		cwarn << "*************************************************************";
		exit(0);
	}
	BlockInfo bi(_genesisRLP, RLP(&_genesisRLP)[0].isList() ? BlockData : HeaderData);
	parentHash = bi.parentHash();
	author = bi.author();
	difficulty = bi.difficulty();
	gasLimit = bi.gasLimit();
	gasUsed = bi.gasUsed();
	timestamp = bi.timestamp();
	extraData = bi.extraData();
	genesisState = _state;
	RLP r(_genesisRLP);
	sealFields = r[0].itemCount() - BlockInfo::BasicFields;
	sealRLP.clear();
	for (unsigned i = BlockInfo::BasicFields; i < r[0].itemCount(); ++i)
		sealRLP += r[0][i].data();

	auto b = genesisBlock();
	if (b != _genesisRLP)
	{
		cdebug << "Block passed:" << bi.hash() << bi.hash(WithoutSeal);
		cdebug << "Genesis now:" << BlockInfo::headerHashFromBlock(b);
		cdebug << RLP(b);
		cdebug << RLP(_genesisRLP);
		throw 0;
	}
}

ChainParams::ChainParams(std::string const& _json)
{
	js::mValue val;
	json_spirit::read_string(_json, val);
	js::mObject obj = val.get_obj();
	sealEngineName = obj["sealEngine"].get_str();
	js::mObject genesis = obj["genesis"].get_obj();
	js::mObject params = obj["params"].get_obj();

	// params
	{
		accountStartNonce = u256(fromBigEndian<u256>(fromHex(params["accountStartNonce"].get_str())));
		maximumExtraDataSize = u256(fromBigEndian<u256>(fromHex(params["maximumExtraDataSize"].get_str())));
		blockReward = u256(fromBigEndian<u256>(fromHex(params["blockReward"].get_str())));
		for (auto i: params)
			if (i.first != "accountStartNonce" && i.first != "maximumExtraDataSize" && i.first != "blockReward")
				otherParams[i.first] = i.second.get_str();
	}

	// genesis
	{
		genesisState = jsonToAccountMap(_json);
		parentHash = h256(genesis["parentHash"].get_str());
		author = genesis.count("coinbase") ? h160(genesis["coinbase"].get_str()) : h160(genesis["author"].get_str());
		difficulty = genesis.count("difficulty") ? u256(fromBigEndian<u256>(fromHex(genesis["difficulty"].get_str()))) : 0;
		gasLimit = u256(fromBigEndian<u256>(fromHex(genesis["gasLimit"].get_str())));
		gasUsed = genesis.count("gasUsed") ? u256(fromBigEndian<u256>(fromHex(genesis["gasUsed"].get_str()))) : 0;
		timestamp = u256(fromBigEndian<u256>(fromHex(genesis["timestamp"].get_str())));
		extraData = bytes(fromHex(genesis["extraData"].get_str()));

		// magic code for handling ethash stuff:
		if ((genesis.count("mixhash") || genesis.count("mixHash")) && genesis.count("nonce"))
		{
			h256 mixHash(genesis[genesis.count("mixhash") ? "mixhash" : "mixHash"].get_str());
			h64 nonce(genesis["nonce"].get_str());
			sealFields = 2;
			sealRLP = rlp(mixHash) + rlp(nonce);
		}
	}
}

SealEngineFace* ChainParams::createSealEngine()
{
	SealEngineFace* ret = SealEngineRegistrar::create(sealEngineName);
	ret->setChainParams(*this);
	if (sealRLP.empty())
	{
		sealFields = ret->sealFields();
		sealRLP = ret->sealRLP();
	}
	return ret;
}

h256 ChainParams::calculateStateRoot() const
{
	MemoryDB db;
	SecureTrieDB<Address, MemoryDB> state(&db);
	state.init();
	if (!stateRoot)
	{
		// TODO: use hash256
		//stateRoot = hash256(toBytesMap(gs));
		dev::eth::commit(genesisState, state);
		stateRoot = state.root();
	}
	return stateRoot;
}

bytes ChainParams::genesisBlock() const
{
	RLPStream block(3);

	calculateStateRoot();

	block.appendList(BlockInfo::BasicFields + sealFields)
			<< parentHash
			<< EmptyListSHA3	// sha3(uncles)
			<< author
			<< stateRoot
			<< EmptyTrie		// transactions
			<< EmptyTrie		// receipts
			<< LogBloom()
			<< difficulty
			<< 0				// number
			<< gasLimit
			<< gasUsed			// gasUsed
			<< timestamp
			<< extraData;
	block.appendRaw(sealRLP, sealFields);
	block.appendRaw(RLPEmptyList);
	block.appendRaw(RLPEmptyList);
	return block.out();
}
