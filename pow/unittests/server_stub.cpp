// Copyright 2018 The Beam Team
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "pow/external_pow.h"
#include "node/node.h"
#include "utility/io/reactor.h"
#include "utility/io/timer.h"
#include "utility/logger.h"
#include <future>
#include <boost/filesystem.hpp>

using namespace beam;

std::unique_ptr<IExternalPOW> server;
int id = 0;
Merkle::Hash hash;
Block::PoW POW;
static const unsigned TIMER_MSEC = 280000;
io::Timer::Ptr feedJobsTimer;

void got_new_block();

void gen_new_job() {
    ECC::GenRandom(&POW.m_Nonce, Block::PoW::NonceType::nBytes);
    ECC::GenRandom(POW.m_Indices.data(), Block::PoW::nSolutionBytes);
    //ECC::GenRandom(&pow.m_Difficulty.m_Packed, 4);
    POW.m_Difficulty = 0; //Difficulty(1 << Difficulty::s_MantissaBits);
    ECC::GenRandom(&hash.m_pData, 32);

    if (server) server->new_job(
        std::to_string(++id),
        hash, POW,
        &got_new_block,
        []() { return false; }
    );

    feedJobsTimer->start(TIMER_MSEC, false, &gen_new_job);
}

void got_new_block() {
    feedJobsTimer->cancel();
    if (server) {
        std::string blockId;
        server->get_last_found_block(blockId, POW);
        if (POW.IsValid(hash.m_pData, 32)) {
            LOG_INFO() << "got valid block" << TRACE(blockId);
        }
        gen_new_job();
    }
}

void find_certificates(IExternalPOW::Options& o) {
#define CERT_FILE_NAME "test.crt"
#define KEY_FILE_NAME "test.key"
#define UNITTEST_PATH PROJECT_SOURCE_DIR "/utility/unittest/"
    using namespace boost::filesystem;
    if (exists("./" CERT_FILE_NAME) && exists("./" KEY_FILE_NAME)) {
        o.certFile = CERT_FILE_NAME;
        o.privKeyFile = KEY_FILE_NAME;
    } else if (exists(UNITTEST_PATH CERT_FILE_NAME) && exists(UNITTEST_PATH KEY_FILE_NAME)) {
        o.certFile = UNITTEST_PATH CERT_FILE_NAME;
        o.privKeyFile = UNITTEST_PATH KEY_FILE_NAME;
    }
#undef UNITTEST_PATH
#undef CERT_FILE_NAME
#undef KEY_FILE_NAME
}

void run_without_node() {
    io::Address listenTo = io::Address::localhost().port(20000);
    io::Reactor::Ptr reactor = io::Reactor::create();
    io::Reactor::Scope scope(*reactor);
    io::Reactor::GracefulIntHandler gih(*reactor);
    feedJobsTimer = io::Timer::create(*reactor);
    IExternalPOW::Options options;
    find_certificates(options);
    server = IExternalPOW::create(options, *reactor, listenTo);
    gen_new_job();
    reactor->run();
    feedJobsTimer.reset();
    server.reset();
}

void run_with_node() {
    boost::filesystem::remove_all("xxxxx");
    boost::filesystem::remove_all("yyyyy");

    io::Address listenTo = io::Address().port(20000);
    io::Reactor::Ptr reactor = io::Reactor::create();
    io::Reactor::Scope scope(*reactor);
    io::Reactor::GracefulIntHandler gih(*reactor);
    feedJobsTimer = io::Timer::create(*reactor);
    IExternalPOW::Options options;
    find_certificates(options);
    server = IExternalPOW::create(options, *reactor, listenTo);

    Rules::get().StartDifficulty = 0;
    Rules::get().UpdateChecksum();
    LOG_INFO() << "Rules signature: " << Rules::get().Checksum;

    Node node;
    node.m_Cfg.m_sPathLocal = "xxxxx";
    node.m_Cfg.m_Listen.port(10000);
    node.m_Cfg.m_Listen.ip(0);
    node.m_Cfg.m_MiningThreads = 0;
    node.m_Cfg.m_VerificationThreads = 1;
    uintBig_t<256> fakeKdf;
    fakeKdf.m_pData[0] = 33;
    node.m_Keys.InitSingleKey(fakeKdf);

    //std::shared_ptr<ECC::HKdf> pKdf(new ECC::HKdf);
    //pKdf->m_Secret.V.m_pData[0] = 33;
    //node.m_pKdf = pKdf;

    Node dummyNode;
    dummyNode.m_Cfg.m_sPathLocal = "yyyyy";
    dummyNode.m_Cfg.m_Listen.port(10001);
    dummyNode.m_Cfg.m_Listen.ip(0);
    dummyNode.m_Cfg.m_MiningThreads = 0;
    dummyNode.m_Cfg.m_VerificationThreads = 1;
    dummyNode.m_Cfg.m_Connect.push_back(io::Address::localhost().port(10000));

    node.Initialize(server.get());
    node.get_Processor().m_Extra.m_SubsidyOpen = false;

    dummyNode.Initialize();
    reactor->run();
}

int main(int argc, char* argv[]) {
    ECC::InitializeContext();

    auto logger = Logger::create(LOG_LEVEL_INFO, LOG_LEVEL_VERBOSE);
    int retCode = 0;
    try {
        if (argc > 1 && argv[1] == std::string("-n")) {
            run_with_node();
        } else {
            run_without_node();
        }
        LOG_INFO() << "Done";
    } catch (const std::exception& e) {
        LOG_ERROR() << "EXCEPTION: " << e.what();
        retCode = 255;
    } catch (...) {
        LOG_ERROR() << "NON_STD EXCEPTION";
        retCode = 255;
    }
    return retCode;

}

