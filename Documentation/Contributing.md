Akaros Contribution Policies
===========================
**2019-03-14** Barret Rhoden (`brho`)

Interested in contributing to Akaros?  This document covers coding standards,
version control usage, and other contribution policies.

Contents
---------------------------
+ Licensing
+ Contribution Philosophy
+ Contribution Steps
+ Coding Standard
+ Git Workflow
    - Setup
    - A Basic Commit
    - A Bigger Change: Several Commits!
    - Someone Changed origin/master; My Branch is Now Old
    - Oh No, I Committed and Pushed Something I Want to Change
    - Crap, Someone Reset a Branch I was Tracking!
    - What Happens When Two People `push -f`
    - Before You Submit Patches: checkpatch
    - How To Submit Patches: adt
    - How To Submit Patches: git send-email
    - How To Submit Patches: git request-pull


Licensing
---------------------------
By contributing to Akaros, you are agreeing with the Developer Certificate of
Origin (found in [Documentation/Developer_Certificate_of_Origin](Developer_Certificate_of_Origin)
and at <http://developercertificate.org/>).

All contributions of any sort, including code from other projects, must be
compatible with the GPLv2.

All new code should be licensed "GPLv2 or later".

You (or your employer) may retain the copyright to your contributions, if
applicable.  We do not require copyright assignment.

Your git 'Sign-off' indicates your agreement with these rules.


Contribution Philosophy
---------------------------
"Do the right thing."  If you've discovered a problem, figure out how to fix it
and fix it right.  You do not need to solve every problem, but think a little
and don't be short-sighted.  (Grep the codebase for TODO.)  For example, if you
want to disable the legacy USB devices, then go ahead.  You do not need to
write an entire USB stack, but you should expect that stack to be written in
the future.

You 'own' your patches and changes, meaning both that they are your
responsibility and you are free to pursue a solution in your own way.  You must
be happy with your commits; I must be ok with them.  If you're happy with
something half-assed, then you're in the wrong line of work.

Expect push-back on various things and plan on reworking your commits.  We
should be able to reach a good solution through discussion and, on occasion,
arguments.

Discussion and arguments about patches and technical content is fine.
Attacking people is not.


Contribution Steps
---------------------------
Contributing to Akaros is much like contributing to the Linux kernel.

The short version of adding code to Akaros:
+ Email the list early and often
+ Send-email or push to your own repo/branch and request-pull
+ Get someone (brho) to shepherd your work
+ Fix up your commits until they are merged

Contact the list before starting on a major project.  I may already have plans
or initial thoughts on the matter, and someone on the list can provide advice.
Likewise, don't break long term plans or be too invasive for no reason.
Otherwise, I might not accept the change at all.

The end goal for a major change is to have a sane series of patches that
applies cleanly to `origin/master`.  Sane, means there are not a lot of "oh wait,
this patch is wrong, reverting".  The patch series should tell a story of
getting from A to B, where each patch is easily reviewable.

If your change is minor, like a bug fix, then feel free to just fix it and
submit the patch; you don't need to ask before fixing a bug.

Before you submit your patch, run scripts/checkpatch.pl on it.  Checkpatch has
both false positives and false negatives, but at least run it and fix the
obviously incorrect changes.  Email the mailing list if you have any questions.
See below for how to use checkpatch.

To submit your patchset, you have two options: git send-email or git
request-pull.  Send-email will take each of your patches and send it to the
mailing list.  Request-pull requires you to push your work to a branch in your
own repo (probably on github), and it will output a block of text.  You then
email that output to the mailing list with a suitable subject.  See below for
how to do a send-email or request-pull.

Once your patches or request-pull has been emailed to the list, someone, usually
me, will shepherd your work, providing feedback and TODOs.  The TODOs usually
consist of fixing commits, such that your patchset does not introduce errors at
intermediate patches.  This means you will need to rewrite commits and git's
history.  It is not enough to just add an extra commit on top of broken patches
in your patchset. (See below for some git commands and examples).

After you fixed a patchset, you send-email or request pull again, and you do so
in response to your original email.  For request-pull, this is easy: you just
hit reply to the first message.  For send-email, you use the --in-reply-to=
option.

In general, I prefer not to merge, but it may be necessary based on the age of
the changed branch.  If we scale out and have various submaintainers, we'll
merge those branches into the main repo.  But at this point, the `brho:origin/`
repo is a 'leaf' (the one and only leaf).  We'll hold off on merging until we
have many leaves.

If you bring in code from another project:
+ First add the code, unaltered if possible, in a single commit.  That commit
must say where you got the code from, such as a URL, project name and version,
etc.  Do not add the .c files to Kbuild yet.  Otherwise, you'll break the build.

+ Run spatch and clang-format, if applicable, in a single commit.  It's much
easier to diagnose problems if spatch and reformatting was done before any
other changes.  Sometimes these tools fail.  Just fix up any mistakes in this
commit.

+ Add your own changes, such that it compiles.  This is the point where you add
the .c files to Kbuild so that we actually build the files.


A few final notes:
+ You must sign-off your commits.

+ Your commits should be of a reasonable size.  Try to isolate your commits to
logically consistent, easily verifiable changes.  Each commit should compile,
but you don't want to change everything in one commit.

+ If a patch relied on spatch, provide the commands or scripts used in the
commit message.  If the script is likely to be useful beyond your patch, then
put it in scripts/spatch/ so we can reuse it or learn from it.

+ Remember that someone other than yourself has to look at each of these
commits.


Coding Standard
---------------------------
We use the Linux kernel coding style.  When in doubt, do what they do.

+ If you are bringing in code from another project other than Linux, such as
  Plan 9, consider running clang-format on it.  We have a format file in the
  root of the Akaros repo.  clang-format isn't perfect, but it's close enough.
  We do this for Plan 9 code, which we heavily internalize, but if the code is
  a library (random, crypto, zlib) that we won't touch, then just leave it as
  is.  That makes it easier to update the library.

+ You'll note that old code may lack the correct style, especially due to the
  large bodies of code from other systems involved.  Typically, we fix it when
  fixing the code for other reasons, so that git blame points at the most
  recent relevant change.

+ We can do major reformatting commits and have git blame ignore them.  Talk
  with brho before doing major reformatting.

+ Indentation is done with tabs, 8-spaces wide.

+ No spaces after the name of a function in a call.  For example,
  `printk("hello")` not `printk ("hello")`.

+ Functions that take no arguments are declared `f(void)` not `f()`.

+ Function names are all lower-case, separated by underscores.

+ One space after commas.  For example `foo(x, y, z)`, not `foo(x,y,z)`.

+ One space after keywords `if`, `for`, `while`, `switch`.  For example,
  `if (x)` not `if(x)`.

+ Space before braces.  For example, `if (x) {` not `if (x){`.

+ For `if/while/etc`, the opening brace is on the same line as the `if`.  If you
  have just one line, no brace is necessary.  If you have an `if / else` clause,
  and one of them has braces, put braces on both.  An `else` line should start
  with a brace.

+ You can use tabs or spaces for indentation, so long as the spaces follow the
  tabs and they are not intermixed.  Older code used tabs for indentation, and
  spaces for formatting/aligning.  Using spaces for alignment  was easier when
  we changed from four-space tabs to eight-space tabs.  That style is also
  eaiser for some scenarios and editors.  Don't worry too much about it.

+ Preprocessor macros are usually upper-case.  Try to avoid using macros
  unless necessary (I prefer static inlines).

+ Pointer declarations have the `*` on the variable: `void *x`, not `void* x`.

+ Multi-word names are lower_case_with_underscores.  Keep your names short,
  especially for local variables.

+ 'Permanent' comments in are C `/* ... */` comments.  Only use C++ style (`//`)
  for temporary commenting-out, such as if you want to not call a function.
  If you want to block out large chunks of code, use `#if 0 ... #endif`

+ In a function definition, the function name should NOT start a new line.  Old
  code incorrectly does this, and should be changed over time.

+ Feel free to use `goto`, especially in functions that have cleanup before
  they exit, or in other places that enhance readability.

+ 80 characters per line.  When you wrap, try to line things up so they make
  sense, such as space-indenting so function arguments line up vertically.

+ Do not `typedef` structs.  You can `typedef` function pointers when convenient.
  Keep in mind that typedefs require people to look up the real type when they
  analyze the code.

+ Try to avoid making lots of separate files or extravagant directory
  hierarchies.  Keep in mind that people will need to work with and find these
  files (even via tab completion, it can be a pain).

+ Consider aligning the members of structs with multiple values.  I often align
  the members (not the types) at 40 chars.  This is a stylistic choice.

Git Workflow
------------------------------------
Git allows us to do a lot of cool things.  One of its primary purposes is to
provide a means for us to review each others code.  In this section, I'll
describe how I use git to make changes to Akaros and send those changes to the
mailing list.

There are a bunch of scripts and tools in scripts/git/.  I'll use some of them
below.

### Setup

First off, visualizing the git tree is extremely useful.  I use:

`$ gitk --all &`

If you only have a terminal, you can use git log for a similar effect:

`$ git log --graph --full-history --all --color --pretty=format:"%x1b[31m%h%x09%x1b[32m%d%x1b[0m%x20%s%x20%x1b[33m(%an)%x1b[0m"`

That's pretty crazy, so I have the following in my `~/.gitconfig`

```
[alias]
	    gr  = log --graph --full-history --all --color --pretty=format:"%x1b[31m%h%x09%x1b[32m%d%x1b[0m%x20%s%x20%x1b[33m(%an)%x1b[0m"
        grs = log --graph --full-history --all --color --pretty=format:"%x1b[31m%h%x09%x1b[32m%d%x1b[0m%x20%s%x20%x1b[33m(%an)%x1b[0m" --simplify-by-decoration
```

With those aliases, I only need to do `git gr`.


### A Basic Commit

Let's say you want to make a minor change to say, the kernel monitor.  To start
things off, make sure you are up to date.

```
$ git checkout master 		# make sure you're on master
$ git fetch 				# grab the latest from origin/
$ git merge origin/master
```

Many people simply do a git pull to get the latest.  I tend to fetch, then
merge manually after glancing at the stream of changes.  If you ever see a
(XCC) in one of the commit messages, then you may need to rebuild your
toolchain.

Time to make our change.  Let's make a branch called `monitor-change`:

`$ git checkout -b monitor-change`

Edit, compile, and test a change, say in `kern/src/monitor.c`

`$ git add -p kern/src/monitor.c`

The `-p` flag allows you to incrementally add and review each diff chunk.  It's
very useful for committing only what you want from a set of changes in your
working directory.

`$ git status`

Take a look at things before you commit; see if you missed any files or added
anything you don't want.

`$ git commit -s`

The -s is to append your "Signed-off-by" line.

Write a descriptive message, with a short, one-line summary first.  e.g.

```
commit 52cc9b19c6edfc183276b1ebf24af9baea4d20d2
Author: Barret Rhoden <brho@cs.berkeley.edu>
Date:   Mon Jan 19 18:36:07 2015 -0500

    Fixes kernel argument checking in "m"

    Need at least one argument before dereferencing.

    Also, debugcmd is pretty handy.
```

Oh wait, say you messed up the commit message or some part of the code.  You
can make the change and amend the commit.

```
$ vi kern/src/monitor.c
$ git add -p kern/src/monitor.c
$ git commit --amend 			# can also change the commit message
```

Now that you are happy, push it.

`$ git push origin monitor-change`

Email the mailing list or your nearest shepherd, and we'll take a look!


### A Bigger Change: Several Commits!

Say you are working on the monitor, and you realize your change is quite large
and probably should be several smaller, logically consistent commits.

You may have already added some of the changes to the index.  To reset the
index (what git will commit in the next commit):

`$ git reset HEAD`

Now, your changes are unstaged.  Confirm with:

`$ git status`

Now you want to add some, but not all, of your changes to `monitor.c`:

`$ git add -p kern/src/monitor.c kern/src/other_files_too.c`

Select only the changes you want, then commit:

`$ git commit -s  # write message`

Then make further commits:

```
$ git add -p kern/src/monitor ; git commit -s
$ git add -p kern/src/monitor ; git commit -s

$ git push origin monitor-change
```


### Someone Changed origin/master; My Branch is Now Old

Typically, we want branches to descend from `origin/master`.  But if multiple
people are working at a time, someone will fall behind.  With git, dealing with
this situation is extremely easy.  You just need to rebase your current branch
on the new master.

Say you are on the branch `monitor-change`, and `origin/master` has updated.
Here's a reasonable workflow to deal with this situation:

```
$ git fetch 				# notice origin/master changed
$ git checkout master 		# switch to master
$ git merge origin/master 	# fast-forward merge - straight line of commits
```

Master is now up to date.  `git pull --rebase` does this too, though I prefer to
always fetch first.

```
$ git checkout monitor-change 	# needed if you aren't on monitor-change
$ git rebase master
```

Now monitor-change descends from the latest master.

A short-cut for the last two commands is:

`$ git rebase master monitor-change`

The last parameter is simply an implied `git checkout`.

Don't forget to recompile and skim the log from the master update.  If you
already pushed your branch to the repo, you'll want to update that branch.

`$ git push -f origin monitor-change`

The `-f` replaces the `origin/monitor-change` with your `monitor-change`.  See below
for more on `-f`.

If you don't want to bother with maintaining your master right now, you can cut
it down to a few steps (assuming you are on `SOMEBRANCH`):

```
$ git fetch
$ git rebase origin/master
$ git push -f origin/SOMEBRANCH
```


### Oh No, I Committed and Pushed Something I Want to Change

Fear not, adjusting git's history is very easy, and we do it all the time on
branches other than origin/master.

Let's say you have a string of commits, e.g.: (edited from my `git gr`)

```
* 876d5b0        (HEAD, vmmcp, origin/vmmcp) VMM: handle EPT page faults
* f926c02        Fixes VMR creating off-by-one
* f62dd6c        VMM: Removes the epte_t from pte_t   Crap - bugs!
* 08e42d6        VMM: Call EPT ops for every KPT op
* d76b4c6        Redefines PTE present vs mapped      Crap - debug prints!
* ddb9fa7        x86: EPT and KPT are contiguous
* 1234567        (master, origin/master) Some Other Commit
```

You're working on the `vmmcp` branch, and you realize that `d76b4c6` adds a bunch
of debug `printk`s in it and that there is a bug in `HEAD` that you introduced in
`f62dd6c`.

The lousy option would be to make a new commit on top of `876d5b0 (HEAD)` that
removes the prints and fixes the bugs.  The problem with this is that there is
no reason for people to even know those existed in the first place, and by
leaving the bugs and `printk`s in old code you only increase the load on your
shepherd.

Not to worry, you can use `git rebase -i` to interactively replay the commits
from `master..vmmcp` and rewrite the commits along the way.

First up, make sure you are on `vmmcp` and do a `git rebase -i`.

```
$ git checkout vmmcp
$ git rebase -i master
```

This will replay all commits from (`1234567`, `876d5b0`] (not including `1234567`) on
top of `master` (`1234567`).  Check out `git help rebase` for more info,
including some nifty diagrams.  In short, that `rebase` command is the shorthand
for:

`$ git rebase -i --onto master master vmmcp`

The `rebase` command `-i` option (interactive) will pop open an editor, where you
can change the order of commits, squash multiple commits into a single commit,
or stop and edit the commit.  Note that commits are presented in the order in
which they will be committed, which is the opposite of `gitk` and `git gr`, where
the newest commit is on top.

Let's edit the commit `d76b4c6` (change '`pick`' to '`e`').  Save and exit, and the
rebase will proceed.  When git hits that commit, it will stop and you can amend
your commit.

```
$ vi somefile 			# make your changes
$ git add -p somefile  	# add them to the index (building a commit)
$ git commit --amend 	# amend your *old* commit (d76b4c6)
$ git status 			# see how you are doing
$ git rebase --continue
```

Now check it out in `gitk` or `git gr`.  It's like your debug `printk`s never
existed.

Another approach is to build a temporary commit and use it as a fixup commit.
Let's do that to fix the bug in `f62dd6c`.

```
$ git checkout vmmcp 	# start from the top of the tree
$ vi somefile 			# make your changes
$ git add -p somefile	# stage the changes
$ git commit -m WIP-fixes-vmm-bug
```

You now have something like:

```
* 13a95cc        (HEAD, vmmcp) WIP-fixes-vmm-bug
* 876d5b0        (origin/vmmcp) VMM: handle EPT page faults
* f926c02        Fixes VMR creating off-by-one
* f62dd6c        VMM: Removes the epte_t from pte_t   Crap - bugs!
* 08e42d6        VMM: Call EPT ops for every KPT op
* d76b4c6        Redefines PTE present vs mapped      Crap - debug prints!
* ddb9fa7        x86: EPT and KPT are contiguous
* 1234567        (master, origin/master) Some Other Commit
```

Clearly you don't want that WIP commit (WIP = work in progress).  Let's rebase!

`$ git rebase -i master`

Reorder the commits so that the WIP happens after `f62dd6c`, and change the WIP's
command from '`pick`' to '`f`'.

Assuming everything applies cleanly, you're done.  The WIP approach is even
easier than the `rebase` with `amend`.  I use it when a change applies to both the
current tip as well as the faulty commit.

You can even use `git rebase` to logically reorder commits so that together, the
'story' is more cohesive.

But wait, your version of `vmmcp` differs from `origin/vmmcp`!  The repo's `vmmcp`
branch still looks like the old one.  You need to `push -f` to force the repo's
branch to agree with yours:

`$ git push -f origin vmmcp`

This will destroy the original `origin/vmmcp` and replace it with your `vmmcp`.
Anyone who was tracking `origin/vmmcp` will need to reset their branch.  Do not
do this to `origin/master`.

Just remember: `gitk`, `git rebase`, and `git commit --amend` are your friends.


### Crap, Someone Reset a Branch I was Tracking!

Say you were working on `origin/vmmcp` while someone else did the steps from the
previous section.  You do a `git fetch` and see that you differ!

If you don't have any changes from the original `origin/vmmcp`, then you can
simply:

```
$ git checkout vmmcp
$ git reset --hard origin/vmmcp
```

This throws away any differences between your tree and `origin/vmmcp`, in essence
making it replicate the current state of `origin/vmmcp`.  If you had any changes,
you just lost them.  Yikes!  If in doubt, you can make a temp branch before
doing anything dangerous with reset.

`$ git branch temp 		# keeps a pointer called 'temp' to the current tip`

Say you do have changes.  Now what?  If they are just diffs, but not commits,
you can stash them, then replay the changes on the new `vmmcp` branch:

```
$ git stash
$ git reset --hard origin/vmmcp
$ git stash pop         # might not apply cleanly
```

If you did have commits, you'll want to rebase.  You are trying to rebase the
commits from your old `vmmcp` branch to the new one:

`$ git rebase --onto origin/vmmcp PREVIOUS-ORIGIN-VMMCP`

You'll need to look at `gitk`, `git log`, `git gr`, or whatever to find the commit
name (SHA1) of the original `origin/vmmcp` (before you fetched).  For this
reason, you could have a branch called vmmcp-mine for all of your changes, and
keep your branch `vmmcp` around to just track `origin/vmmcp`.  This is what many
people do with `master`: it only tracks `origin/master`, and all changes happen on
separate branches.

Note that you can probably just do a:

`$ git rebase origin/vmmcp`

and git will figure out and exclude which commits you have in common with
`origin/vmmcp` from the rebase.  Check your results in `gitk` or `git gr`.

This can be rather tricky, which is part of the reason why you shouldn't reset
branches that other people rely on (e.g. `origin/master`).  It's fine for small
group or individual work.

Just remember: `gitk` and `git rebase` are your friends.


### What Happens When Two People `push -f`

Whoever pushes most recently is the one to clobber the branch.  The previous
person's commits would only exist in that person's personal repo!  Yikes!

In general, don't clobber (`push -f`) other people's branches without
coordinating first.  I reset my origin branches all the time.  I won't usually
reset other people's branches without checking with them.

If clobbering repo branches is a problem, you can always create your own repo
branch; branches are very cheap and easy.

To delete a remote branch, say after it is merged into `origin/master`:

`$ git push -f origin :BRANCH-NAME`

The : is part of the "refspec"; whatever is on the left is the source (in this
case, nothing).  Check out `git help push` for more info.

Again, these techinques work well with small groups, but not for big branches
like `origin/master`.


### Before You Submit Patches: checkpatch

scripts/checkpatch.pl is modified from Linux's checkpatch and takes a patchfile as input.  I wrote a wrapper script for it that operates on a branch: scripts/git/git-checkpatch.  I have it in my path, so I can call it like a git subcommand:

```
$ git checkpatch master..my_branch_head
```

That will generate the patches from master..my_branch_head and run checkpatch on each of them in series.
If you don't have the git scripts in your path, you'll have to do something like:

```
$ ./scripts/git/git-checkpatch master..my_branch_head
```

Some developers have a checkpatch pre-commit hook.  It's a personal preference.  This [site](http://blog.vmsplice.net/2011/03/how-to-automatically-run-checkpatchpl.html) suggests the following:
```
	$ cat >.git/hooks/pre-commit
	#!/bin/bash
	exec git diff --cached | scripts/checkpatch.pl --no-signoff -q -
	^D
	$ chmod 755 .git/hooks/pre-commit
```
If checkpatch throws false-positives, then you'll have to bypass the precommit hook with a -n.

For more info on checkpatch, check out http://www.tuxradar.com/content/newbies-guide-hacking-linux-kernel.  We use it just like Linux.


### How To Submit Patches: adt

adt is a script that sends patches to our gerrit repository for a review.
It allows you to send patches for review from the command line, but the
resulting code review is done on our gerrit page.

First, visit https://akaros-review.googlesource.com and sign in with a gmail
account to make sure it creates an account for you.

Next, make sure that you have a remote setup for gerrit.

```
git remote add gerrit https://akaros.googlesource.com/akaros
```

Next, make sure that you have the gerrit hooks installed. This will generate a
Change-ID when you do a commit. If you do this, ignore the checkpatch error
about gerrit Change-IDs.

```
# Install Gerrit Git hooks
curl -Lo .git/hooks/commit-msg https://akaros-review.googlesource.com/tools/hooks/commit-msg
chmod +x .git/hooks/commit-msg
```

Next, suppose you have a branch that contains changes you want to push up,
called vmm. You first do a rebase on top of brho/master, to ensure that your
changes are up to date.

```
git pull --rebase brho master
```

To send the patches up for review, you can then do:

```
scripts/git/adt mail -m dcross,ganshun@google.com,rminnich,brho -t mytopic
```

The three current flags for adt are as follows:
-m specifies your reviewers for this patch,

-cc specifies people you want to be cc'ed on this set of commits.

-t specifies the topic for this set of commits. If this flag is not provided,
the local current branch name will be used as the topic.

### How To Submit Patches: git send-email

git send-email is a simple way to send patches.  It works from the command line
and you don't need a github account or remote repo.

Akaros is no different than Linux or any of a number of projects in our use of
send-email.  For a good overview, check out:
https://www.freedesktop.org/wiki/Software/PulseAudio/HowToUseGitSendEmail/.

Here's the basics for Akaros:

Use the config stub for sendemail in scripts/git/config to send emails to the mailing list.

If your patchset consists of more than one patch, use the --cover-letter option.
This will send an introductory email, called PATCH 0/n, in which you explain
your patch a bit.  If you have just one patch, you don't need to bother with a
cover letter.  Make the subject and message for the cover letter reasonably
descriptive of your overall change.

If you have a remote branch tracking your patchset and are not overwhelmed with
git commands yet, you can put the output of scripts/git/pre-se.sh in the
cover-letter email.  This provides a convenient github link to browse the changes.

If you are using send-email to send new versions of a patch, do it in-reply-to
your original email.  See git help send-email for an example.

Here's an example.  Say I have a branch called 'vmm' and want to send the patches from master to it:

```
$ git send-email --cover-letter master..vmm
```
That sent emails to the mailing list, thanks to the config setting for sendemail.
For your first time, you can send-email to yourself instead:

```
$ git send-email --to=you@gmail.com --cover-letter master..vmm
```

If you want to send a single patch from the tip of the vmm branch:

```
$ git send-email -1 vmm
```

For more info, check out:
+ git help send-email
+ https://www.freedesktop.org/wiki/Software/PulseAudio/HowToUseGitSendEmail/
+ http://www.tuxradar.com/content/newbies-guide-hacking-linux-kernel


### How To Submit Patches: git request-pull

If you have a github account or any place where you can push a branch, you can
use request-pull to submit a patchset.  We also have a custom version of
request-pull that adds extra info into the output:
scripts/git/git-akaros-request-pull.

The usage is simple.  Make sure your branch is up to date on your repo, then do
a request-pull.  'FROM' is the point from which your branch diverges from the
Akaros mainline.  Usually, it'll be 'master'.

```
$ git push REPO BRANCH
$ git akaros-request-pull FROM REPO BRANCH
```

Here's how I would do a request-pull for my example 'vmm' branch:

```
$ git akaros-request-pull master origin vmm
```

Then copy-paste the output into an email and send it to the mailing list.

If you don't have scripts/git/ in your PATH, you can either:

```
$ ./scripts/git/git-akaros-request-pull master origin vmm
```
