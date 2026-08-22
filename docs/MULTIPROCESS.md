# Multiprocess

Transfer Fabric supports process-shared movement through named shared memory. The runtime maps a named region and transfers between a process's host buffers and the shared region; a separate process can open the same region and verify content independently.

See `examples/ex_multiprocess.cpp`: a parent writes a pattern to a named shared region and launches a child that reads and verifies it independently. This is a real, cross-process transfer (not simulated in one process) and is validated across repeated runs.
