#!/usr/bin/python
"""Parses a 'git diff --stat' file to extract what files have been changed as
shown in the diff. Extracts the directory paths of those files, and decides
which components of AKAROS should be compiled and tested, accordingly.
"""
import json
import os
import re
import sys

REGEX_EXTRACT_PATH_FROM_GIT_DIFF_LINE = r'^(?:\s)*([^\s]*)(?:\s)*\|(?:.*)\n?$'
# Path to file with git diff
DIFF_FILE = sys.argv[1]
# Path to file with JSON definition of akaros components
CONFIG_COMP_COMPONENTS_FILE = sys.argv[2]

# Arguments to fill variable paths with. Useful, for example, to define a path
# that will vary whether we are compiling an architecture or another.
# TODO(alfongj): Get these from Env var or sys.argv


"""The following is the definition of the given components of AKAROS (filled in
from CONFIG_COMP_COMPONENTS_FILE (which should be 
config/compilation_components.json or something).

Each 'component' consists of a name (which will be a unique identifier printed
out to console) as key, plus a list of PATHS as content. If any of these paths
is modified, then we will consider that the full component is affected, and
therefore may need to be compiled and tested again.

Paths should be written from the root repo folder, beginning with './' and not
'/', and ending in '/'.

If a path should include any subpaths, then it must be appended with a + symbol.
e.g. ./path/with/subpaths/+

If a path has variable arguments, they should be represented like {{this}}.
These arguments will be filled in with the contents of PATH_ARGS.
e.g. ./path/with/{{VARIABLE}}/{{ARGUMENTS}}/. 
"""
akaros_components = {}

affected_components = {}

def get_variable_path_args() :
	"""Returns dict of arguments to use in the load_component_config function
	to generate dynamic paths. Currently it is only being used to change a 
	subdirectory in one of the paths depending on the architecture being 
	tested.
	"""
	PATH_ARGS = {
		"I686": {
			"arch": "x86"
		},
		"X86_64": {
			"arch": "x86"
		},
		"RISCV": {
			"arch": "riscv"
		}
	}
	compilation_arch = os.getenv('COMPILATION_ARCH', 'I686')
	return PATH_ARGS[compilation_arch]

def load_component_config() :
	"""Loads ../config/compilation_components.json object, which contains a
	list of all the different AKAROS compilation components along with the paths
	to look for for compiling them.
	"""
	conf_file_contents = ""
	# Read config file.
	with open(CONFIG_COMP_COMPONENTS_FILE, 'r') as conf_file :
		conf_file_contents = conf_file.read().replace('\n', '')

	# Replace variable arguments.
	var_path_args = get_variable_path_args()
	for arg in var_path_args :
		wrapped_arg = "{{" + arg + "}}"
		conf_file_contents = conf_file_contents.replace(wrapped_arg, 
		                                                var_path_args[arg])

	# Parse JSON into python object.
	global akaros_components
	akaros_components = json.loads(conf_file_contents)['compilation_components']

def extract_dir(diff_line) :
	"""Given a line from a "git diff --stat" output, it tries to extract a 
	directory from it. 

	If a blank or non-change line is passed, it ignores it and returns nothing.

	If a 'change' line (e.g. ' path/to/file.ext  |  33++ ') is passed, it strips
	the path (not including the file name) and prepends a './' to it and returns
	it.
	"""
	match = re.match(REGEX_EXTRACT_PATH_FROM_GIT_DIFF_LINE, diff_line) 
	if (match) :
		full_path = './' + match.group(1)
		folder_list = full_path.split('/')[0:-1]
		folder_path = '/'.join(folder_list) + '/'
		return folder_path

def includes_subpaths(path) :
	"""Checks if a given path includes subpaths or not. It includes them if it
	ends in a '+' symbol.
	"""
	return path[-1] == '+'

def check_components_affected(path_of_changed_file) :
	"""Checks if a given directory should set the state of one of the components
	to affected.
	"""
	global affected_components
	for component in akaros_components :
		affected = component in affected_components
		paths = akaros_components[component]['PATHS']
		if (not affected) :
			for path in paths :
				if (includes_subpaths(path)) :
					# Checks if a given string contains the given path.
					# e.g., If the path is 'path/to':
						# The regex will match for: 'path/to/file.txt' or 
							# 'path/to/and/subpath/to/file.txt'
						# But not for: 'path/file.txt' nor for 'path/tofile.txt' or
							# 'path/tobananas/file.txt'
					regex = re.compile('^\%s(?:.*/)*$' % path[:-1])
				else :
					# Checks if a given string contains the given path with no 
					# subpaths.
					# e.g., If the path is 'path/to':
						# The regex will match for: 'path/to/file.txt'
						# But not for: 'path/file.txt' nor for 'path/tofile.txt' or
							# 'path/tobananas/file.txt' or 
							# 'path/to/and/subpath/to/file.txt'
					regex = re.compile('^\%s[^/]*$' % path)

				if (re.match(regex, path_of_changed_file)) :
					affected_components[component] = True
					break

def main() :
	load_component_config()
	diff_file = open(DIFF_FILE)
	for line in diff_file :
		cur_dir = extract_dir(line)
		if (cur_dir) :
			check_components_affected(cur_dir)

	print ' '.join(affected_components)

main()
