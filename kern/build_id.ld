/* this is INCLUDEd into the arch-specific ld scripts */

	.note.gnu.build-id :
	{
		. = ALIGN(4);
		PROVIDE(__note_build_id_start = .);
		*(.note.gnu.build-id)
		PROVIDE(__note_build_id_end = .);
	}
