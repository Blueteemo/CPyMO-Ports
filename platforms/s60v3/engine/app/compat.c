/* FP1 Open C headers declare isblank, but the installed libc import library
 * does not export it. CPyMO's script parser runs in the default C locale,
 * whose blank characters are space and horizontal tab.
 * This local implementation supplies the missing C99 function without
 * changing CPyMO's interpreter or interaction logic.
 */
int isblank(int c)
{
    return c == ' ' || c == '\t';
}
