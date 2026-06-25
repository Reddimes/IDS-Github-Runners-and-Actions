# Weekly Activities
## Week 1
### Repository
```
I started reviewing what is needed for the repository.  It seems that the linux runner workflow and windows workflow can exist in the same project.  This means that will only need one repository.  I have created that repository.
```

### Research
To kick off my research for Week 1, I worked on researching what is needed for GitHub runners, actions and workflows.

What I found is that Github classroom is being decommissioned. There are other tools available and you can find more info [here](https://github.com/orgs/community/discussions/196615).  This does not directly affect my independent study though.

After that, I started looking at some Youtube videos to understand the workflows and Github Runners a little better:
- https://youtu.be/mFFXuXjVgkU?si=y4UQW-ZtPlqcayd0
- https://youtu.be/Xwpi0ITkL3U?si=T2zhcO10_cSEKgeO
## Week 2
### Linux Runners
In order to run a linux runner for this, we are going to need to test with two tools:
- [UASM](https://github.com/Terraspace/UASM)
	- This tool is meant for building masm but engineered to work and run on Linux.
- [msvc-wine](https://github.com/mstorsjo/msvc-wine)
	- This tool is actually meant for cross-compilation, but I used it in a container for compiling all my assembly code for class.

Of those tools, I think that UASM will be the better option.  Compiling assembly to run with wine on my local computer presented me a couple of different issues.  For one, the coloring does not properly translate to Linux.  I've never found out how to fix that.  It makes sense that using a compiler meant for Linux would translate that much better.  The second issue is debugging.  While this Independent Study does not focus on using a `devcontainer`, it should be noted that this could be an option for the future.  I have not fully researched `devcontainers` yet, but we currently have one for CPP at this university.  It does not have the debugger, but I feel like that is something that would be better for Assembly.
### Research
During my research this week, I discovered that there is already a Github Action for setting up MASM to run on a Github Runner with Visual Studio 2026. [Setup MASM](https://github.com/marketplace/actions/setup-masm)

There is not a lot of documentation around it, so it may require some testing.  This may be something that I require a little bit of help with, I have not worked primarily with Visual Studio in quite some time.  I imagine that will be be building a Visual Studio Project with masm included.