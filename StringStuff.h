#pragma once

int doesCsvStringBeginWith(const char * startstring, const char * thestring, size_t sizeofthestring)
{
    //note: if "thestring" is empty, this comparison returns 0 (false)
    //
    int thedigit;
    int comparedigit;

    int matches = 0;

    size_t index_of_first_printable_char_in_thestring = 0;

    size_t index_of_start_string = 0;

    //ignore whitespace at beginning
    for (size_t i=0; i<sizeofthestring; ++i) {
        thedigit = thestring[i];
        if (thedigit > 32) {
            index_of_first_printable_char_in_thestring = i;
            break; //for
        } else {
            if (thedigit == 0) {
                //printf("Reached end of thestring without finding visible char\n\n");
                return 0;
            }
        }
    }
    //printf("index of first printable char is %i\n\n", index_of_first_printable_char_in_thestring);

    //now just look for direct match
    for (size_t i=index_of_first_printable_char_in_thestring; i<sizeofthestring; ++i) {
        thedigit = thestring[i];
        comparedigit = startstring[index_of_start_string++];
        //printf("the value of i is %i\n", i);

        //printf("|%c| is the csv string digit, |%c| is the comparison digit ", thedigit, comparedigit);
        if (thedigit == 0) {
            if (comparedigit == 0) {
                //printf("Perfect Match");
                return 1;
            } else {
                //printf("not a match, startstring is longer than thestring");
                return 0;
            }
        }

        if (comparedigit > 0) {
            if (thedigit != comparedigit) {
                //printf("...Digits dont match\n\n");
                return 0;
            } else {
                //printf("...Digits match\n\n");
                ;
            }
        } else {
            //printf("...Match. Reached the end of startstring\n\n");
            return 1;
        }
    }

    //printf("Reached Size Limit, Returning\n\n");
    return 0;
}


int extractIntFromCsvString(uint whichColumn, const char * thestring, size_t sizeofthestring)
{
    int thenumber = 0;
    int thedigit = 0;

    unsigned int commas = 1;
    unsigned int doNegate = 0;
    unsigned int checkMinus = 1;
    unsigned int quotefound = 0;

    for (size_t i=0; i<sizeofthestring; ++i) {
        thedigit = thestring[i];

        if (thedigit == 0) {
            //printf("Reached Null Terminator\n\n");
            return thenumber;
        } else if (thedigit == ',') {
            if (!quotefound) {
                //printf("|%c| Found a comma\n", thedigit);
                if (++commas > whichColumn) {
                    //printf("Early Return\n\n");
                    return thenumber;
                }
                checkMinus = 1;
                doNegate = 0;
                quotefound = 0;
            } else {
                ;
                //printf("|%c| ignoring the comma because inside double quote\n", thedigit);
            }
        } else if (thedigit == '"') {
            quotefound = !quotefound;
            //if (quotefound) {
                //printf("begin ignoring a double-quote string\n");
            //} else {
                //printf("found the matching end double-quote\n");
            //}
            //you can break this.
            // that's totally fine and on you.
        } else if (checkMinus && thedigit == '-') {
            //printf("|%c| Found a minus\n", thedigit);
            doNegate = 1;
            checkMinus = 0;
        } else if (commas==whichColumn && !quotefound) {
            if ( thedigit >= '0' && thedigit <= '9') {
                //printf("|%c| is a digit\n", thedigit);
                if (doNegate) {
                    thenumber = thenumber * 10 - (thedigit - '0');
                } else {
                    thenumber = thenumber * 10 + (thedigit - '0');
                }
            } else {
                ;
                //printf("|%c| is ignored\n", thedigit);
            }
        } else {
            ;
            //printf("|%c| is whatever\n", thedigit);
        }
    }
    //printf("Reached Size Of The String Return\n\n");
    return thenumber;
}


int extractSubstringFromCsvString(char * p_substring, size_t maxsizeofsubstring, uint whichColumn, const char * thestring, size_t sizeofthestring)
{
    //finds a double-quote delimited string from the given column number.
    //the return int is false (0) when wasn't found, and substring is set to zero length string.
    //tries to do the right thing if something was found but it ended wrongly.
    char thechar = 0;

    unsigned int commas = 1;
    unsigned int quotefound = 0;
    unsigned int doextraction = 0;
    unsigned int substringfound = 0;

    size_t index_substring = 0;

    p_substring[0] = 0;     //just in case.

    for (size_t i=0; i<sizeofthestring; ++i) {
        thechar = thestring[i];

        if (thechar == 0) {
            //printf("Reached Null Terminator\n\n");
            p_substring[index_substring] = 0;
            return substringfound;
        } else if (thechar == ',') {
            if (!quotefound) {
                //printf("|%c| Found a comma\n", thechar);
                if (++commas > whichColumn) {
                    //printf("Early Return\n\n");
                    p_substring[index_substring] = 0;
                    return substringfound;
                }
                quotefound = 0;
                doextraction = 0;
            } else {
                if (doextraction) {
                    //printf("|%c| including the comma inside double quote\n", thechar);

                    if (index_substring >= maxsizeofsubstring) {
                        //printf("Substring Full, nevermind\n");
                        p_substring[maxsizeofsubstring - 1] = 0;
                        return 1;
                    }  else {
                        p_substring[index_substring++] = thechar;
                    }
                } else {
                    //printf("|%c| ignoring the comma inside double quote\n", thechar);
                }
            }
        } else if (thechar == '"') {
            quotefound = !quotefound;
            if (quotefound) {
                //printf("beginning a double-quote string\n");
                if (commas==whichColumn) {
                    //printf("this is the column asked for\n");
                    doextraction = 1;
                    substringfound = 1;
                }
            } else {
                //printf("found the matching end double-quote\n");
                if (doextraction) {
                    //printf("returning at ending quote\n");
                    p_substring[index_substring] = 0;
                    return substringfound;
                }
            }
        } else if (commas==whichColumn && doextraction) {
            //printf("|%c| is inserted into the substring\n", thechar);
            if (index_substring >= maxsizeofsubstring) {
                //printf("Substring Full, nevermind\n");
                p_substring[maxsizeofsubstring - 1] = 0;
                return 1;
            }  else {
                p_substring[index_substring++] = thechar;
            }
        } else {
            //printf("|%c| is skipped\n", thechar);
        }
    }
    //printf("Reached Size Of The String Return\n\n");
    p_substring[index_substring] = 0;
    return substringfound;
}


size_t base16encode(char * outstr, size_t maxsizeoutstr, const char * inpstr, size_t maxsizeinpstr)
{
    //converts for example the ASCII string "AX\n" into ascii hexadecimal "4158\n"
    //sizeof(outstr) needs to be 3 or greater. typically make it an odd number.
    // if the last possible pair of chars cannot both fit, it is skipped.
    // returns the number of inpstr ASCII chars processed.
    size_t j=0;
    char value=0;
    char uppernibble=0;
    char lowernibble=0;

    size_t maxsize = maxsizeinpstr;
    if (maxsizeoutstr < 2*maxsizeinpstr) {
        maxsize = (maxsizeoutstr - 1) / 2;
    }
    //printf("\nmax size: %i\n", maxsize);

    for (size_t i=0; i<maxsize; ++i) {
        value = inpstr[i];
        //printf("loop %i, char[%c]\n", i, value);
        if (value == 0) {
            maxsize = i;
            break;
        }
        uppernibble = (value >> 4) & 0xF;
        outstr[j++] = (uppernibble < 10) ? '0' + uppernibble : 'A' - 10 + uppernibble;
        lowernibble = value & 0xF;
        outstr[j++] = (lowernibble < 10) ? '0' + lowernibble : 'A' - 10 + lowernibble;
    }
    outstr[j] = 0;
    return maxsize;
}
