#
# properly rename include guards (needs perl)
echo "Checking include guards"
find . -type f -name "*.h" | grep -v "squirrel/" | while read f; do guard="$(echo $f | cut -b 3- | tr '[[:lower:]]' '[[:upper:]]' | tr -C '[[:alnum:]]' '_' | rev | cut -c2- | rev)"; perl -i -p0e "s/(\n){2,}#ifndef [^\n]*\n#define [^\n]*(\n)+/\n\n#ifndef $guard\n#define $guard\n\n\n/" $f; done

#
# remove trailing spaces
echo "Removing trailing whitespaces"
# perl instead of sed: BSD sed (macOS) treats "-i -e" as a backup suffix and does not know \t
find . -type f \( -name "*.h" -o -name "*.cc" \) | grep -v "squirrel" | xargs perl -pi -e 's/[ \t]+$//'
