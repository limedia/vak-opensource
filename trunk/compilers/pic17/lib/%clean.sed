/^#/d
/^ *$/d
s/; *\([L0-9]\)/\
\1/g
s/; */\
	/g
s/  */ /g
s/^  */	/
s/:  */:	/g