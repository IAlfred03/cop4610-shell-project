# Shell

[Description]

## Group Members
- **Carter Mason**: cbm20a@fsu.edu
- **Derrick Larkins**: js19@fsu.edu
- **Isaiah Alfred**: ab19@fsu.edu
## Division of Labor

## Division of Labor
- **Isaiah Alfred**: Implemented `pipeline_exec.c`, `exec.c`, redirection handling, and tilde/env expansion.  
- **Derrick Larkins**: Worked on `parser.c`, `lexer.c`, and `prompt.c`.  
- **Carter Mason**: Built `builtins.c`, `jobs.c`, and integrated `Makefile` + testing harness.  

---

## File Listing
.
├── Makefile # Build rules for compiling and running tests
├── README.md # Project documentation
├── bin/ # Compiled executables
│ └── c_tests # Executable for C tests
├── include/ # Header files
│ ├── builtins.h # Built-in command declarations
│ ├── exec.h # Execution functions and exec options
│ ├── jobs.h # Job control structures/functions
│ ├── lexer.h # Lexer declarations
│ ├── parser.h # Parser declarations
│ └── prompt.h # Prompt handling declarations
├── src/ # Source files
│ ├── builtins.c # Implementation of built-in shell commands
│ ├── exec.c # Core execution functions
│ ├── expand.c # Environment/tilde expansion helpers
│ ├── jobs.c # Background job tracking
│ ├── lexer.c # Lexical analysis for command input
│ ├── main.c # Entry point of the shell
│ ├── parser.c # Parse input into pipeline structures
│ ├── pipe.c # Pipe setup logic
│ ├── pipeline_exec.c # Execute pipelines of commands
│ ├── prompt.c # Display and manage shell prompt
│ └── redir.c # Redirection handling
└── tests/ # Unit and functional tests
├── a_tests.c # Person-A tests (prompt/lexer/parser)
├── b_tests.c # Person-B tests (builtins)
├── c_tests.c # Person-C tests (exec/pipeline/jobs)
└── tmp/ # Temporary output files used by tests
├── basic.txt
├── bytes.txt
├── count.txt
├── env.txt
├── in.txt
├── jobs.txt
├── out.txt
├── pwdlen.txt
├── redir.txt
└── tilde.txt

yaml
Copy code

---

## How to Compile and Run
To compile everything:
```bash
make
To run individual test suites:

bash
Copy code
make atest   # Run Person-A tests
make btest   # Run Person-B tests
make ctest   # Run Person-C tests
To clean build artifacts:

bash
Copy code
make clean
Known Bugs / Unfinished Portions
Currently, tilde_expansion and builtin_jobs tests fail.

mkdir_p_for_file has issues on WSL when resolving /mnt/... paths. This may cause redirection tests to fail.

All other features (env expansion, cd, exit, pipelines, background jobs) pass successfully.

Development Log
Isaiah Alfred: Implemented execution path resolution, redirection handling, and debugging pipeline_exec.c.

Teammate 2: Focused on parsing logic and error handling in parser.c.

Teammate 3: Handled builtin commands, Makefile integration, and test verification.

Group Meetings
Weekly sync during recitations to split up implementation tasks.

Debugging sessions held before deadlines to integrate all components and resolve linker/compiler issues.

Special Considerations
The project was developed in WSL (Ubuntu on Windows). File path behavior (/mnt/...) differs slightly from linprog, but the project compiles and runs under standard POSIX/Linux environments.

All binaries are located in the bin/ directory. No object files or executables are committed.
