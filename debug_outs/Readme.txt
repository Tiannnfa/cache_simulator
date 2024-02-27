These are the debug logs for the traces run through the TA simulator.

If you want to compare line-by-line, you will need to do the same printf()s at
equivalent places in your code. We suggest putting those inside `#ifdef`s as
follows:

    #ifdef DEBUG
    printf("L1 hit\n");
    #endif

This way, running `make clean` and `make DEBUG=1` will include the copious
debug output, but running just `make` (which we will run when grading and
comparing with non-debug solution traces) will leave them out.

For your convenience, debug_printfs.txt holds all the debug printf()s we used
in our simulator. The subsequent arguments to printf() after the format string
have been replaced with "...", so you'll need to replace that placeholder with
the proper values for your code.

