PROG            = fact screendemo
COBC		= cobc -x

all:		$(PROG)

clean:
		rm -f *~ $(PROG)

%:		%.cob
		$(COBC) $< -o $@
