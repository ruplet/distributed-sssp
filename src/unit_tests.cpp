#include "block_dist.hpp"

const bool VERBOSE = false;

bool testBlockDist() {
    // test trivial distribution
    {
        auto dist = BlockDistribution::Distribution(0, 0);
        if (dist.getNResponsibleVertices(0).has_value()) { logError("Shouldn't have value!"); return false; }
        if (dist.getNResponsibleVertices(1).has_value()) { logError("Shouldn't have value!"); return false; }
        if (dist.getNResponsibleVertices(100).has_value()) { logError("Shouldn't have value!"); return false; }
        if (dist.getResponsibleProcessor(0).has_value()) { logError("Shouldn't have value!"); return false; }
        if (dist.getResponsibleProcessor(100).has_value()) { logError("Shouldn't have value!"); return false; }
    }
    // test constructor exception
    {
        try {
            BlockDistribution::Distribution(0, 1);
            logError("Shouldn't be able to create!"); return false;
        } catch (const BlockDistribution::Distribution::InvalidDistribution& e) {
        } catch (const std::exception& ex) {
            logError(std::string("Unexpected exception thrown: ") + ex.what()); return false;
        } catch (...) {
            logError("Unknown non-std::exception thrown"); return false;
        }

        try {
            BlockDistribution::Distribution(0, 100);
            logError("Shouldn't be able to create!"); return false;
        } catch (const BlockDistribution::Distribution::InvalidDistribution& e) {
        } catch (const std::exception& ex) {
            logError(std::string("Unexpected exception thrown: ") + ex.what()); return false;
        } catch (...) {
            logError("Unknown non-std::exception thrown"); return false;
        }
    }
    // test uniform distribution
    {
        auto dist = BlockDistribution::Distribution(1, 19);
        if (!dist.getResponsibleProcessor(0).has_value()) { logError("Should have value!"); return false; }
        if (*dist.getResponsibleProcessor(0) != 0) { logError("Invalid owner!"); return false; }
        if (!dist.getResponsibleProcessor(1).has_value()) { logError("Should have value!"); return false; }
        if (*dist.getResponsibleProcessor(1) != 0) { logError("Invalid owner!"); return false; }
        if (!dist.getResponsibleProcessor(10).has_value()) { logError("Should have value!"); return false; }
        if (*dist.getResponsibleProcessor(10) != 0) { logError("Invalid owner!"); return false; }
        if (!dist.getResponsibleProcessor(18).has_value()) { logError("Should have value!"); return false; }
        if (*dist.getResponsibleProcessor(18) != 0) { logError("Invalid owner!"); return false; }
        if (dist.getResponsibleProcessor(19).has_value()) { logError("Shouldn't have value!"); return false; }
    }
    {
        auto dist = BlockDistribution::Distribution(2, 4);
        if (!dist.getResponsibleProcessor(0).has_value()) { logError("Should have value!"); return false; }
        if (*dist.getResponsibleProcessor(0) != 0) { logError("Invalid owner!"); return false; }
        if (!dist.getResponsibleProcessor(1).has_value()) { logError("Should have value!"); return false; }
        if (*dist.getResponsibleProcessor(1) != 0) { logError("Invalid owner!"); return false; }
        if (!dist.getResponsibleProcessor(2).has_value()) { logError("Should have value!"); return false; }
        if (*dist.getResponsibleProcessor(2) != 1) { logError("Invalid owner!"); return false; }
        if (!dist.getResponsibleProcessor(3).has_value()) { logError("Should have value!"); return false; }
        if (*dist.getResponsibleProcessor(3) != 1) { logError("Invalid owner!"); return false; }
        if (dist.getResponsibleProcessor(4).has_value()) { logError("Shouldn't have value!"); return false; }
    }
    {
        size_t nProc = 17;
        size_t vertPerProc = 13;
        auto dist = BlockDistribution::Distribution(nProc, nProc * vertPerProc);
        if (!dist.getNResponsibleVertices(0).has_value()) { logError("Should have value!"); return false; }
        if (*dist.getNResponsibleVertices(0) != vertPerProc) { logError("Invalid number of owned!"); return false; }
        if (!dist.getNResponsibleVertices(1).has_value()) { logError("Should have value!"); return false; }
        if (*dist.getNResponsibleVertices(1) != vertPerProc) { logError("Invalid number of owned!"); return false; }
        if (!dist.getNResponsibleVertices(15).has_value()) { logError("Should have value!"); return false; }
        if (*dist.getNResponsibleVertices(15) != vertPerProc) { logError("Invalid number of owned!"); return false; }
        if (!dist.getNResponsibleVertices(16).has_value()) { logError("Should have value!"); return false; }
        if (*dist.getNResponsibleVertices(16) != vertPerProc) { logError("Invalid number of owned!"); return false; }

        if (!dist.getResponsibleProcessor(0).has_value()) { logError("Should have value!"); return false; }
        if (*dist.getResponsibleProcessor(0) != 0) { logError("Invalid owner!"); return false; }
        if (!dist.getResponsibleProcessor(1).has_value()) { logError("Should have value!"); return false; }
        if (*dist.getResponsibleProcessor(1) != 0) { logError("Invalid owner!"); return false; }
        if (!dist.getResponsibleProcessor(vertPerProc - 1).has_value()) { logError("Should have value!"); return false; }
        if (*dist.getResponsibleProcessor(vertPerProc - 1) != 0) { logError("Invalid owner!"); return false; }

        if (!dist.getResponsibleProcessor(vertPerProc).has_value()) { logError("Should have value!"); return false; }
        if (*dist.getResponsibleProcessor(vertPerProc) != 1) { logError("Invalid owner!"); return false; }

        if (!dist.getResponsibleProcessor(nProc * vertPerProc - vertPerProc - 1).has_value()) { logError("Should have value!"); return false; }
        if (*dist.getResponsibleProcessor(nProc * vertPerProc - vertPerProc - 1) != nProc - 2) { logError("Invalid owner!"); return false; }

        if (!dist.getResponsibleProcessor(nProc * vertPerProc - vertPerProc).has_value()) { logError("Should have value!"); return false; }
        if (*dist.getResponsibleProcessor(nProc * vertPerProc - vertPerProc) != nProc - 1) { logError("Invalid owner!"); return false; }
        if (!dist.getResponsibleProcessor(nProc * vertPerProc - 1).has_value()) { logError("Should have value!"); return false; }
        if (*dist.getResponsibleProcessor(nProc * vertPerProc - 1) != nProc - 1) { logError("Invalid owner!"); return false; }

        if (dist.getResponsibleProcessor(nProc * vertPerProc).has_value()) { logError("Shouldn't have value!"); return false; }
        if (dist.getResponsibleProcessor(nProc * vertPerProc * 100 + 9999).has_value()) { logError("Shouldn't have value!"); return false; }

    }
    // test non-uniform distribution
    {
        // [2] * 2 + [1] * 15
        auto dist = BlockDistribution::Distribution(17, 19);
        if (!dist.getNResponsibleVertices(0).has_value()) { logError("Should have value!"); return false; }
        if (*dist.getNResponsibleVertices(0) != 2) { logError("Invalid number of owned!"); return false; }
        if (!dist.getNResponsibleVertices(1).has_value()) { logError("Should have value!"); return false; }
        if (*dist.getNResponsibleVertices(1) != 2) { logError("Invalid number of owned!"); return false; }
        if (!dist.getNResponsibleVertices(3).has_value()) { logError("Should have value!"); return false; }
        if (*dist.getNResponsibleVertices(3) != 1) { logError("Invalid number of owned!"); return false; }
        if (!dist.getNResponsibleVertices(16).has_value()) { logError("Should have value!"); return false; }
        if (*dist.getNResponsibleVertices(16) != 1) { logError("Invalid number of owned!"); return false; }

        if (!dist.getResponsibleProcessor(0).has_value()) { logError("Should have value!"); return false; }
        if (*dist.getResponsibleProcessor(0) != 0) { logError("Invalid owner!"); return false; }
        if (!dist.getResponsibleProcessor(1).has_value()) { logError("Should have value!"); return false; }
        if (*dist.getResponsibleProcessor(1) != 0) { logError("Invalid owner!"); return false; }
        if (!dist.getResponsibleProcessor(2).has_value()) { logError("Should have value!"); return false; }
        if (*dist.getResponsibleProcessor(2) != 1) { logError("Invalid owner!"); return false; }
        if (!dist.getResponsibleProcessor(3).has_value()) { logError("Should have value!"); return false; }
        if (*dist.getResponsibleProcessor(3) != 1) { logError("Invalid owner!"); return false; }
        if (!dist.getResponsibleProcessor(4).has_value()) { logError("Should have value!"); return false; }
        if (*dist.getResponsibleProcessor(4) != 2) { logError("Invalid owner!"); return false; }
        if (!dist.getResponsibleProcessor(5).has_value()) { logError("Should have value!"); return false; }
        if (*dist.getResponsibleProcessor(5) != 3) { logError("Invalid owner!"); return false; }
        if (!dist.getResponsibleProcessor(6).has_value()) { logError("Should have value!"); return false; }
        if (*dist.getResponsibleProcessor(6) != 4) { logError("Invalid owner!"); return false; }
        if (!dist.getResponsibleProcessor(18).has_value()) { logError("Should have value!"); return false; }
        if (*dist.getResponsibleProcessor(18) != 16) { logError("Invalid owner!"); return false; }

        if (dist.getResponsibleProcessor(19).has_value()) { logError("Shouldn't have value!"); return false; }
    }
    {
        // [4] * 16 + [3]
        auto dist = BlockDistribution::Distribution(17, 51 + 16);
        if (!dist.getNResponsibleVertices(0).has_value()) { logError("Should have value!"); return false; }
        if (*dist.getNResponsibleVertices(0) != 4) { logError("Invalid number of owned!"); return false; }
        if (!dist.getNResponsibleVertices(1).has_value()) { logError("Should have value!"); return false; }
        if (*dist.getNResponsibleVertices(1) != 4) { logError("Invalid number of owned!"); return false; }
        if (!dist.getNResponsibleVertices(15).has_value()) { logError("Should have value!"); return false; }
        if (*dist.getNResponsibleVertices(15) != 4) { logError("Invalid number of owned!"); return false; }
        if (!dist.getNResponsibleVertices(16).has_value()) { logError("Should have value!"); return false; }
        if (*dist.getNResponsibleVertices(16) != 3) { logError("Invalid number of owned!"); return false; }

        if (!dist.getResponsibleProcessor(0).has_value()) { logError("Should have value!"); return false; }
        if (*dist.getResponsibleProcessor(0) != 0) { logError("Invalid owner!"); return false; }
        if (!dist.getResponsibleProcessor(1).has_value()) { logError("Should have value!"); return false; }
        if (*dist.getResponsibleProcessor(1) != 0) { logError("Invalid owner!"); return false; }
        if (!dist.getResponsibleProcessor(2).has_value()) { logError("Should have value!"); return false; }
        if (*dist.getResponsibleProcessor(2) != 0) { logError("Invalid owner!"); return false; }
        if (!dist.getResponsibleProcessor(3).has_value()) { logError("Should have value!"); return false; }
        if (*dist.getResponsibleProcessor(3) != 0) { logError("Invalid owner!"); return false; }

        if (!dist.getResponsibleProcessor(4).has_value()) { logError("Should have value!"); return false; }
        if (*dist.getResponsibleProcessor(4) != 1) { logError("Invalid owner!"); return false; }
        if (!dist.getResponsibleProcessor(5).has_value()) { logError("Should have value!"); return false; }
        if (*dist.getResponsibleProcessor(5) != 1) { logError("Invalid owner!"); return false; }
        if (!dist.getResponsibleProcessor(6).has_value()) { logError("Should have value!"); return false; }
        if (*dist.getResponsibleProcessor(6) != 1) { logError("Invalid owner!"); return false; }
        if (!dist.getResponsibleProcessor(7).has_value()) { logError("Should have value!"); return false; }
        if (*dist.getResponsibleProcessor(7) != 1) { logError("Invalid owner!"); return false; }

        if (!dist.getResponsibleProcessor(60).has_value()) { logError("Should have value!"); return false; }
        if (*dist.getResponsibleProcessor(60) != 15) { logError("Invalid owner!"); return false; }
        if (!dist.getResponsibleProcessor(61).has_value()) { logError("Should have value!"); return false; }
        if (*dist.getResponsibleProcessor(61) != 15) { logError("Invalid owner!"); return false; }
        if (!dist.getResponsibleProcessor(62).has_value()) { logError("Should have value!"); return false; }
        if (*dist.getResponsibleProcessor(62) != 15) { logError("Invalid owner!"); return false; }
        if (!dist.getResponsibleProcessor(63).has_value()) { logError("Should have value!"); return false; }
        if (*dist.getResponsibleProcessor(63) != 15) { logError("Invalid owner!"); return false; }

        if (!dist.getResponsibleProcessor(64).has_value()) { logError("Should have value!"); return false; }
        if (*dist.getResponsibleProcessor(64) != 16) { logError("Invalid owner!"); return false; }
        if (!dist.getResponsibleProcessor(65).has_value()) { logError("Should have value!"); return false; }
        if (*dist.getResponsibleProcessor(65) != 16) { logError("Invalid owner!"); return false; }
        if (!dist.getResponsibleProcessor(66).has_value()) { logError("Should have value!"); return false; }
        if (*dist.getResponsibleProcessor(66) != 16) { logError("Invalid owner!"); return false; }

        if (dist.getResponsibleProcessor(67).has_value()) { logError("Shouldn't have value!"); return false; }
        else if (VERBOSE) {
            std::cerr << "Distributing 67 vertices among 17 processors. Vertex 67 is not owned by anyone!\n";
        }
    }
    
    if (VERBOSE) {
        auto dist = BlockDistribution::Distribution(7, 30);
        std::cerr << "Distributing 30 vertices among 7 processors.\n";
        std::cerr << "Processor 0 is responsible for: " << *dist.getNResponsibleVertices(0) << " vertices.\n";
        std::cerr << "Processor 1 is responsible for: " << *dist.getNResponsibleVertices(1) << " vertices.\n";
        std::cerr << "Processor 2 is responsible for: " << *dist.getNResponsibleVertices(2) << " vertices.\n";
        std::cerr << "Processor 6 is responsible for: " << *dist.getNResponsibleVertices(6) << " vertices.\n";
        std::cerr << "Vertex 0 is owned by processor " << *dist.getResponsibleProcessor(0) << "\n";
        std::cerr << "Vertex 1 is owned by processor " << *dist.getResponsibleProcessor(1) << "\n";
        std::cerr << "Vertex 2 is owned by processor " << *dist.getResponsibleProcessor(2) << "\n";
        std::cerr << "Vertex 3 is owned by processor " << *dist.getResponsibleProcessor(3) << "\n";
        std::cerr << "Vertex 4 is owned by processor " << *dist.getResponsibleProcessor(4) << "\n";
        std::cerr << "Vertex 5 is owned by processor " << *dist.getResponsibleProcessor(5) << "\n";
        std::cerr << "Vertex 29 is owned by processor " << *dist.getResponsibleProcessor(29) << "\n";
        if (dist.getResponsibleProcessor(30).has_value()) {
            logError("Vertex 30 is owned by someone, which is incorrect!\n");
            return false;
        } {
            std::cerr << "Vertex 30 is not owned by any processor\n";
        }
    }

    std::cerr << "BlockDistribution::Distribution test successfull!\n";
    return true;
}

int main() {
    if (!testBlockDist()) { return 1; }
    
    return 0;
}