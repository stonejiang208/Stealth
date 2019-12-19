// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2012 The Bitcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "main.h"
#include "bitcoinrpc.h"

using namespace json_spirit;
using namespace std;

extern QPRegistry *pregistryMain;

extern void TxToJSON(const CTransaction& tx, const uint256 hashBlock, json_spirit::Object& entry);

static const unsigned int SEC_PER_DAY = 86400;

double GetDifficulty(const CBlockIndex* blockindex)
{
    // Floating point number that is a multiple of the minimum difficulty,
    // minimum difficulty = 1.0.
    if (blockindex == NULL)
    {
        if (pindexBest == NULL)
        {
            return 1.0;
        }
        else
        {
            blockindex = GetLastBlockIndex(pindexBest, false);
        }
    }

    if (GetFork(blockindex->nHeight) >= XST_FORKQPOS)
    {
        return 0.0;
    }

    int nShift = (blockindex->nBits >> 24) & 0xff;

    double dDiff =
        (double)0x0000ffff / (double)(blockindex->nBits & 0x00ffffff);

    while (nShift < 29)
    {
        dDiff *= 256.0;
        nShift++;
    }
    while (nShift > 29)
    {
        dDiff /= 256.0;
        nShift--;
    }

    return dDiff;
}

double GetPoSKernelPS()
{
    int nPoSInterval = 72;
    double dStakeKernelsTriedAvg = 0;
    int nStakesHandled = 0, nStakesTime = 0;

    CBlockIndex* pindex = pindexBest;;
    CBlockIndex* pindexPrevStake = NULL;

    while (pindex && nStakesHandled < nPoSInterval)
    {
        if (pindex->IsProofOfStake())
        {
            dStakeKernelsTriedAvg += GetDifficulty(pindex) * 4294967296.0;
            nStakesTime += pindexPrevStake ? (pindexPrevStake->nTime - pindex->nTime) : 0;
            pindexPrevStake = pindex;
            nStakesHandled++;
        }

        pindex = pindex->pprev;
    }

    return nStakesTime ? dStakeKernelsTriedAvg / nStakesTime : 0;
}

Object blockToJSON(const CBlock& block, const CBlockIndex* blockindex, bool fPrintTransactionDetail)
{
    Object result;
    result.push_back(Pair("hash", block.GetHash().GetHex()));
    
    if (block.IsQuantumProofOfStake())
    {
        int nConfs = pindexBest->nHeight + 1 - blockindex->nHeight;
        if (blockindex->IsInMainChain())
        {
            result.push_back(Pair("isinmainchain", true));
            result.push_back(Pair("confirmations", nConfs));
        }
        else
        {

            result.push_back(Pair("isinmainchain", false));
            result.push_back(Pair("confirmations", 0));
            result.push_back(Pair("depth", nConfs));
        }
    }
    else
    {
        CMerkleTx txGen(block.vtx[0]);
        txGen.SetMerkleBranch(&block);
        result.push_back(Pair("confirmations", (int)txGen.GetDepthInMainChain()));
    }
    result.push_back(Pair("size", (int)::GetSerializeSize(block, SER_NETWORK, PROTOCOL_VERSION)));
    result.push_back(Pair("height", blockindex->nHeight));
    result.push_back(Pair("version", block.nVersion));
    result.push_back(Pair("merkleroot", block.hashMerkleRoot.GetHex()));
    if (blockindex->IsQuantumProofOfStake())
    {
        result.push_back(Pair("staker_id", (boost::int64_t)block.nStakerID));
        string sAlias;
        if (pregistryMain->GetAliasForID(block.nStakerID, sAlias))
        {
            result.push_back(Pair("staker_alias", sAlias));
        }
        if (blockindex->pprev != NULL)
        {
            result.push_back(Pair("block_reward",
              ValueFromAmount(GetQPoSReward(blockindex->pprev))));
        }
    }
    result.push_back(Pair("mint", ValueFromAmount(blockindex->nMint)));
    result.push_back(Pair("time", (boost::int64_t)block.GetBlockTime()));
    result.push_back(Pair("nonce", (boost::uint64_t)block.nNonce));
    result.push_back(Pair("bits", HexBits(block.nBits)));
    result.push_back(Pair("difficulty", GetDifficulty(blockindex)));

    if (blockindex->pprev)
        result.push_back(Pair("previousblockhash", blockindex->pprev->GetBlockHash().GetHex()));
    if (blockindex->pnext)
        result.push_back(Pair("nextblockhash", blockindex->pnext->GetBlockHash().GetHex()));
    string sFlags;
    if (blockindex->IsQuantumProofOfStake())
    {
        sFlags = "quantum-proof-of-stake";
    }
    else if (blockindex->IsProofOfStake())
    {
        sFlags = "proof-of-stake";
    }
    else
    {
        sFlags = "proof-of-work";
    }
    result.push_back(Pair("flags", strprintf("%s%s", sFlags.c_str(), blockindex->GeneratedStakeModifier()? " stake-modifier": "")));
    result.push_back(Pair("proofhash", blockindex->IsProofOfStake()? blockindex->hashProofOfStake.GetHex() : blockindex->GetBlockHash().GetHex()));
    if (blockindex->IsProofOfStake())
    {
        result.push_back(Pair("entropybit", (int)blockindex->GetStakeEntropyBit()));
        result.push_back(Pair("modifier", strprintf("%016" PRIx64, blockindex->nStakeModifier)));
        result.push_back(Pair("modifierchecksum", strprintf("%08x", blockindex->nStakeModifierChecksum)));
    }
    Array txinfo;
    BOOST_FOREACH (const CTransaction& tx, block.vtx)
    {
        if (fPrintTransactionDetail)
        {
            Object entry;

            entry.push_back(Pair("txid", tx.GetHash().GetHex()));
            TxToJSON(tx, 0, entry);

            txinfo.push_back(entry);
        }
        else
            txinfo.push_back(tx.GetHash().GetHex());
    }

    result.push_back(Pair("tx", txinfo));
    result.push_back(Pair("signature", HexStr(block.vchBlockSig.begin(), block.vchBlockSig.end())));

    return result;
}

Value getbestblockhash(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
            "getbestblockhash\n"
            "Returns the hash of the best block in the longest block chain.");

    return hashBestChain.GetHex();
}

Value getblockcount(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
            "getblockcount\n"
            "Returns the number of blocks in the longest block chain.");
    return nBestHeight;
}


Value getdifficulty(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
            "getdifficulty\n"
            "Returns the difficulty as a multiple of the minimum difficulty.");

    Object obj;
    obj.push_back(Pair("proof-of-work",        GetDifficulty()));
    obj.push_back(Pair("proof-of-stake",       GetDifficulty(GetLastBlockIndex(pindexBest, true))));
    obj.push_back(Pair("search-interval",      (int)nLastCoinStakeSearchInterval));
    return obj;
}


Value settxfee(const Array& params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 1 || AmountFromValue(params[0]) < MIN_TX_FEE)
        throw runtime_error(
            "settxfee <amount>\n"
            "<amount> is a real and is rounded to the nearest 0.01");

    nTransactionFee = AmountFromValue(params[0]);
    nTransactionFee = (nTransactionFee / CENT) * CENT;  // round to cent

    return true;
}

Value getrawmempool(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
            "getrawmempool\n"
            "Returns all transaction ids in memory pool.");

    vector<uint256> vtxid;
    mempool.queryHashes(vtxid);

    Array a;
    BOOST_FOREACH(const uint256& hash, vtxid)
        a.push_back(hash.ToString());

    return a;
}

Value getblockhash(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
            "getblockhash <index>\n"
            "Returns hash of block in best-block-chain at <index>.");

    int nHeight = params[0].get_int();
    if (nHeight < 0 || nHeight > nBestHeight)
        throw runtime_error("Block number out of range.");

    CBlockIndex* pblockindex = FindBlockByHeight(nHeight);
    return pblockindex->phashBlock->GetHex();
}

Value getblock(const Array& params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 2)
        throw runtime_error(
            "getblock <hash> [txinfo]\n"
            "txinfo optional to print more detailed tx info\n"
            "Returns details of a block with given block-hash.");

    std::string strHash = params[0].get_str();
    uint256 hash(strHash);

    if (mapBlockIndex.count(hash) == 0)
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found");

    CBlock block;
    CBlockIndex* pblockindex = mapBlockIndex[hash];
    block.ReadFromDisk(pblockindex, true);

    return blockToJSON(block, pblockindex, params.size() > 1 ? params[1].get_bool() : false);
}

Value getblockbynumber(const Array& params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 2)
        throw runtime_error(
            "getblockbynumber <number> [txinfo]\n"
            "txinfo optional to print more detailed tx info\n"
            "Returns details of a block with given block-number.");

    int nHeight = params[0].get_int();
    if (nHeight < 0 || nHeight > nBestHeight)
        throw runtime_error("Block number out of range.");

    CBlock block;
    CBlockIndex* pblockindex = mapBlockIndex[hashBestChain];
    while (pblockindex->nHeight > nHeight)
        pblockindex = pblockindex->pprev;

    uint256 hash = *pblockindex->phashBlock;

    pblockindex = mapBlockIndex[hash];
    block.ReadFromDisk(pblockindex, true);

    return blockToJSON(block, pblockindex, params.size() > 1 ? params[1].get_bool() : false);
}


#if 0
Value getwindowedblockinterval(const Array& params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 3)
        throw runtime_error(
            "getwindowedtxvolume <period> <windowsize> <windowspacing>\n"
            "  last window ends at time of most recent block\n"
            "  - <period> : duration over which to calculate (sec)\n"
            "  - <windowsize> : duration of each window (sec)\n"
            "  - <windowspacing> : duration between start of consecutive windows (sec)\n"
            "Returns an object with attributes:\n"
            "  - window_start: starting time of each window\n"
            "  - number_blocks: number of plocks in each window\n"
            "  - tx_volume: number of transactions in each window\n");
# endif


Value getwindowedtxvolume(const Array& params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 3)
        throw runtime_error(
            "getwindowedtxvolume <period> <windowsize> <windowspacing>\n"
            "  last window ends at time of most recent block\n"
            "  - <period> : duration over which to calculate (sec)\n"
            "  - <windowsize> : duration of each window (sec)\n"
            "  - <windowspacing> : duration between start of consecutive windows (sec)\n"
            "Returns an object with attributes:\n"
            "  - window_start: starting time of each window\n"
            "  - number_blocks: number of plocks in each window\n"
            "  - tx_volume: number of transactions in each window");

    int nPeriod = params[0].get_int();
    if (nPeriod < 1)
    {
        throw runtime_error(
            "Period should be greater than 0.\n");
    }
    if ((unsigned int) nPeriod > 36525 * SEC_PER_DAY)
    {
        throw runtime_error(
            "Period should be less than 100 years.\n");
    }

    int nWindow = params[1].get_int();
    if (nWindow < 1)
    {
        throw runtime_error(
            "Window size should be greater than 0.\n");
    }
    if (nWindow > nPeriod)
    {
        throw runtime_error(
            "Window size should be less than or equal to period.\n");
    }

    int nGranularity = params[2].get_int();
    if (nGranularity < 1)
    {
        throw runtime_error(
            "Window spacing should be greater than 0.\n");
    }
    if (nGranularity > nWindow)
    {
        throw runtime_error(
            "Window spacing should be less than or equal to window.\n");
    }

    if (pindexBest == NULL)
    {
        throw runtime_error("No blocks.\n");
    }

    unsigned int nTime = pindexBest->nTime;

    // asdf use different block
    if (nTime < pindexGenesisBlock->nTime)
    {
        throw runtime_error("TSNH: Invalid block time.\n");
    }

    unsigned int nPeriodEnd = nTime;
    unsigned int nPeriodStart = 1 + nPeriodEnd - nPeriod;

    vector<unsigned int> vBlockTimes;
    vector<unsigned int> vNumberTxs;
    CBlockIndex *pindex = pindexBest;
    while (pindex->pprev)
    {
        vBlockTimes.push_back(nTime);
        vNumberTxs.push_back(pindex->nTxVolume);
        pindex = pindex->pprev;
        nTime = pindex->nTime;
        if (nTime < nPeriodStart)
        {
            break;
        }
    }

    std::reverse(vBlockTimes.begin(), vBlockTimes.end()); 
    std::reverse(vNumberTxs.begin(), vNumberTxs.end()); 

    unsigned int nSizePeriod = vBlockTimes.size();

    Array aryWindowStartTimes;
    Array aryTotalBlocks;
    Array aryTotalTxs;

    unsigned int nWindowStart = nPeriodStart;
    unsigned int nWindowEnd = nWindowStart + nWindow - 1;

    unsigned int idx = 0;
    unsigned int idxNext = 0;
    bool fNextUnknown = true;

    while (nWindowEnd < nPeriodEnd)
    {
        if (fNextUnknown)
        {
            idxNext = idx;
        }
        else
        {
            fNextUnknown = true;
        }
        unsigned int nNextWindowStart = nWindowStart + nGranularity;
        unsigned int nWindowBlocks = 0;
        unsigned int nWindowTotal = 0;
        for (idx = idxNext; idx < nSizePeriod; ++idx)
        {
            printf("idx is: %u\n", idx);
            unsigned int nBlockTime = vBlockTimes[idx];
            // assumes blocks are chronologically ordered
            if (nBlockTime > nWindowEnd)
            {
                aryWindowStartTimes.push_back((boost::int64_t)nWindowStart);
                aryTotalTxs.push_back((boost::int64_t)nWindowTotal);
                aryTotalBlocks.push_back((boost::int64_t)nWindowBlocks);
                nWindowStart = nNextWindowStart;
                nWindowEnd += nGranularity;
                break;
            }
            nWindowBlocks += 1;
            nWindowTotal += vNumberTxs[idx];
            if (fNextUnknown && (nBlockTime >= nNextWindowStart))
            {
                idxNext = idx;
                fNextUnknown = false;
            }
        }
    }

    Object obj;
    obj.push_back(Pair("window_start", aryWindowStartTimes));
    obj.push_back(Pair("number_blocks", aryTotalBlocks));
    obj.push_back(Pair("tx_volume", aryTotalTxs));

    return obj;
}


// ppcoin: get information of sync-checkpoint
Value getcheckpoint(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
            "getcheckpoint\n"
            "Show info of synchronized checkpoint.\n");

    Object result;
    CBlockIndex* pindexCheckpoint;

    result.push_back(Pair("synccheckpoint", Checkpoints::hashSyncCheckpoint.ToString().c_str()));
    pindexCheckpoint = mapBlockIndex[Checkpoints::hashSyncCheckpoint];        
    result.push_back(Pair("height", pindexCheckpoint->nHeight));
    result.push_back(Pair("timestamp", DateTimeStrFormat(pindexCheckpoint->GetBlockTime()).c_str()));
    if (mapArgs.count("-checkpointkey"))
        result.push_back(Pair("checkpointmaster", true));

    return result;
}
