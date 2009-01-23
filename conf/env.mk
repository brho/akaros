# env.mk - configuration variables for the JOS lab


# '$(V)' controls whether the lab makefiles print verbose commands (the
# actual shell commands run by Make), as well as the "overview" commands
# (such as '+ cc lib/readline.c').
#
# For overview commands only, the line should read 'V = @'.
# For overview and verbose commands, the line should read 'V ='.
V = @


# '$(HANDIN_EMAIL)' is the email address to which lab handins should be
# sent.
HANDIN_EMAIL = 6.828-handin@pdos.lcs.mit.edu


##
## If your system-standard GNU toolchain is ELF-compatible, then comment
## out the following line to use those tools (as opposed to the i386-jos-elf
## tools that the 6.828 make system looks for by default).
##
# GCCPREFIX=''
