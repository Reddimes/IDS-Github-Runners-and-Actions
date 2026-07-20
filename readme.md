# Weekly Activities
## Week 1
### Repository
I started reviewing what is needed for the repository.  It seems that the Linux runner workflow and windows workflow can exist in the same project.  This means that will only need one repository.  I have created that repository.
### Research
To kick off my research for Week 1, I worked on researching what is needed for GitHub runners, actions and workflows.

What I found is that GitHub classroom is being decommissioned. There are other tools available and you can find more info [here](https://github.com/orgs/community/discussions/196615).  This does not directly affect my independent study though.

After that, I started looking at some Youtube videos to understand the workflows and Github Runners a little better:
- https://youtu.be/mFFXuXjVgkU?si=y4UQW-ZtPlqcayd0
- https://youtu.be/Xwpi0ITkL3U?si=T2zhcO10_cSEKgeO
---
## Week 2
### Linux Runners
In order to run a Linux runner for this, we are going to need to test with two tools:
- [UASM](https://github.com/Terraspace/UASM)
	- This tool is meant for building MASM but engineered to work and run on Linux.
- [msvc-wine](https://github.com/mstorsjo/msvc-wine)
	- This tool is actually meant for cross-compilation, but I used it in a container for compiling all my assembly code for class.

Of those tools, I think that UASM will be the better option.  Compiling assembly to run with wine on my local computer presented me a couple of different issues.  For one, the coloring does not properly translate to Linux.  I've never found out how to fix that.  It makes sense that using a compiler meant for Linux would translate that much better.  The second issue is debugging.  While this Independent Study does not focus on using a `devcontainer`, it should be noted that this could be an option for the future.  I have not fully researched `devcontainers` yet, but we currently have one for CPP at this university.  It does not have the debugger, but I feel like that is something that would be better for Assembly.
### Research
During my research this week, I discovered that there is already a GitHub Action for setting up MASM to run on a GitHub Runner with Visual Studio 2026. [Setup MASM](https://github.com/marketplace/actions/setup-masm)

There is not a lot of documentation around it, so it may require some testing.  This may be something that I require a little bit of help with, I have not worked primarily with Visual Studio in quite some time.  I imagine that will be be building a Visual Studio Project with MASM included.
## Week 3
### Research
I discovered a [MASM Runner](https://marketplace.visualstudio.com/items?itemName=istareatscreens.masm-runner) for VSCode which appears to be compatible with Linux and Mac. This might be useful when it comes to setting up a `devcontainer`.

One a side note, I've been researching what is required for GitHub Certifications and these are the classes I need:
- [GitHub Foundations](https://gale.udemy.com/course/gh-900-exam-prep-github-foundations/)
- [GitHub Actions](https://gale.udemy.com/course/mastering-github-actions-beginner-to-expert/learn/lecture/41767880#overview)
- GitHub Administration

For this Independent Study, I intend to stick mostly closely to the GitHub Actions course as that is the essential to what I need.
---
## Week 4
### Single-File Compilation Container
I consolidated the Docker setup into a single-file compilation container. Instead of mounting an entire `src/` directory, you mount one `.asm` file to `/test.asm` and the container compiles, runs, and validates it automatically:
```bash
docker run --rm -v src/RevStr.asm:/test.asm masm-run
# → PASS: RevStr
```
No `-e NAME` environment variable is needed. The entrypoint auto-detects which program it is by comparing the cleaned output against embedded expected outputs.

### MSVC Path Handling
A critical discovery: MSVC's `ml` and `link` interpret `/` as a flag character. All paths must use `z:` drive notation with double backslashes (e.g., `z:\\test.asm`, `z:\\opt\\irvine32`). The `msvcenv.sh` file is patched during the Docker build to prepend `z:\\opt\\irvine32` to `INCLUDE`, `LIB`, and `LIBPATH`.
---
## Week 5
### Wine Console I/O Workaround
Wine's `ReadConsoleA`/`WriteConsoleA` produce no stdout without a pseudo-terminal. The solution is the `script` command, which allocates a pty:
```sh
script -q -c "wine z:\\test.exe" /dev/null > /tmp/test.out 2>&1
```
This was tested and confirmed to be the only reliable way to capture output in headless Docker.

### Output Cleanup
Wine's console output contains ANSI escape codes and `\r\n` line endings. The `sed` command in the entrypoint cleans this:
```sh
sed -i 's/\x1b\[[?0-9;]*[a-zA-Z]//g; s/\r//g' "$ACTUAL"
```
This strips cursor hide/show sequences (`[?25l`, `[?25h`) and carriage returns, leaving plain text for comparison.

### Validation System
The entrypoint embeds expected outputs directly (no external `tests/` directory needed). It loops through known expected outputs to match actual output. A match prints `PASS: <name>`. A mismatch prints the cleaned output for manual review and exits 0.
---
## Week 6
### DOSBox Testing
I explored using DOSBox instead of Wine. Key findings:
- DOSBox emulates DOS, not Windows, so Irvine32 won't work (it's a Windows library)
- DOS programs use `INT 21h` interrupts instead
- DOSBox would need `SDL_VIDEODRIVER=dummy` for headless mode
- Output capture would require framebuffer reading, not stdout
- **Conclusion:** Not viable for Irvine32-based programs

### PowerShell and Batch File Testing
I tested using PowerShell and batch files under Wine for the validation step instead of shell scripting:
- PowerShell (`wine powershell -File`) produces no stdout in headless mode
- PowerShell cannot write files through Wine's path mapping
- Batch files (`wine cmd /c`) have the same stdout limitation
- **Conclusion:** Shell-based validation outside Wine is the correct approach
---
## Week 7
### CI Workflow Sync
Updated the Linux Wine workflow (`masm-wine.yml`) to match the container's proven setup:
- Added `wine32` to apt packages
- Fixed `sed` patch to use `z:\\opt\\irvine32` (double backslash)
- Added SDK lib copies (`kernel32.lib`, `user32.lib` from Windows Kits)
- Changed compile/link to use `z:` paths via `/tmp/test.asm` copy
- Changed output capture to use `script -q -c` with `sed` ANSI/CR cleanup
- Removed the `wine cmd /c` run step (no stdout without pty)
