_spawner_compl() {
	local cur=${COMP_WORDS[COMP_CWORD]}
	COMPREPLY=( $(compgen -c ${cur}) )
}

complete -o default -o nospace -F _spawner_compl strace
complete -o default -o nospace -F _spawner_compl perf
