000010 @OPTIONS MAIN
000020 Identification Division.
000030 Program-Id.  Chapt04a.
000040 Environment Division.
000050 Configuration Section.
000060 Source-Computer.  IBM-PC.
000070 Object-Computer.  IBM-PC.
000080 Data Division.
000090 Working-Storage Section.
000252 01  Dollar-Amount         Pic 9(5)v99 Value 12.99.
000253 01  Item-Description      Pic X(10) Value "Gold Coins".
000254 Screen Section.
000255 01  Main-Screen
000256     Blank Screen, Auto, Required,
000257     Foreground-Color Is 7,
000258     Background-Color Is 1.
000259     03  Line 1 Column 35 Value "Item Entry".
000261     03  Line 3 Column 5  Value "Item Value " Highlight Bell.
000262     03  Line 3 Column 16 Pic $$,$$$.99 From Dollar-Amount.
000263     03  Line 5 Column 5  Value "Item Description" Highlight.
000264     03  Line 5 Column 22 Pic X(10) From Item-Description.
000265     03  Line 6 Column 4  Value "Short Description" Highlight.
000266     03  Line 6 Column 22 Pic X(5) From Item-Description
000267                                        Justified Right.
000285 Procedure Division.
000286 Chapt04a-Start.
000287     Display Main-Screen
000460     Stop Run.