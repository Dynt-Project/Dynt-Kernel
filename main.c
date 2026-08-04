// Written by [@saphhic](https://github.com/saphhic)  
// Date: 29 July 2026

//   this is the main loop for the OS,
//   before the "while (true) hlt();" (its also valid to use the io functions from io.h but for main loop its safer to do it this way)
//   we add the startup function like demostrated
//     example: startup();
//modified in some parrty by @epaxgamingtv

#include "init/startup.h"
#include "arch/x86_64/cpu/cpu.h"

void main() {
    
    startup();

    // the window manager takes over the main loop

    while (true) hlt(); 
}
