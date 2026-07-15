### Experiment Log

*   **Run 1**: Baseline naive sender/receiver on Profile A. Delay 40ms.
    *   **Miss %**: 27.07%
    *   **Overhead**: 1.10x
    *   **Change**: Initial purely ARQ execution test.
    *   **Why**: Identified that RTT is too slow for a strict 40ms deadline; replacement packets arrive late.
*   **Run 2**: Pure ARQ with increased delay to map limits. Profile A, Delay 100ms.
    *   **Miss %**: 0.0%
    *   **Overhead**: 1.05x
    *   **Change**: Pushed delay up to 100ms.
    *   **Why**: Proved the logic was working, but 100ms playout delay is a poor score.
*   **Run 3**: Hybrid FEC + ARQ Implementation. Profile A, Delay 60ms.
    *   **Miss %**: 0.0%
    *   **Overhead**: ~1.55x
    *   **Change**: Grouped frames into pairs and sent a third parity (XOR) packet for every pair.
    *   **Why**: Allows the receiver to instantly recover a dropped frame without waiting for a round-trip NACK, bypassing the network delay entirely while remaining well under the 2.0x limit.
*   **Run 4**: Stress Test Hybrid Logic. Profile B, Delay 80ms.
    *   **Miss %**: ~0.3%
    *   **Overhead**: ~1.58x
    *   **Change**: Testing against higher loss rates and burst drops.
    *   **Why**: Parity handles isolated drops, while the background 50ms ARQ NACK scanner safely cleans up the burst drops.