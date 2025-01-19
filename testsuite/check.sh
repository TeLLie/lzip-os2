#! /bin/sh
# check script for Lzip - LZMA lossless data compressor
# Copyright (C) 2008-2025 Antonio Diaz Diaz.
#
# This script is free software: you have unlimited permission
# to copy, distribute, and modify it.

LC_ALL=C
export LC_ALL
objdir=`pwd`
testdir=`cd "$1" ; pwd`
LZIP="${objdir}"/lzip
framework_failure() { echo "failure in testing framework" ; exit 1 ; }

if [ ! -f "${LZIP}" ] || [ ! -x "${LZIP}" ] ; then
	echo "${LZIP}: cannot execute"
	exit 1
fi

[ -e "${LZIP}" ] 2> /dev/null ||
	{
	echo "$0: a POSIX shell is required to run the tests"
	echo "Try bash -c \"$0 $1 $2\""
	exit 1
	}

if [ -d tmp ] ; then rm -rf tmp ; fi
mkdir tmp
cd "${objdir}"/tmp || framework_failure

cp "${testdir}"/test.txt in || framework_failure
in_lz="${testdir}"/test.txt.lz
fox_lz="${testdir}"/fox.lz
fnz_lz="${testdir}"/fox_nz.lz
fail=0
test_failed() { fail=1 ; printf " $1" ; [ -z "$2" ] || printf "($2)" ; }

printf "testing lzip-%s..." "$2"

"${LZIP}" -fkqm4 in
[ $? = 1 ] || test_failed $LINENO
[ ! -e in.lz ] || test_failed $LINENO
"${LZIP}" -fkqm274 in
[ $? = 1 ] || test_failed $LINENO
[ ! -e in.lz ] || test_failed $LINENO
for i in bad_size -1 0 4095 513MiB 1G 1T 1P 1E 1Z 1Y 10KB ; do
	"${LZIP}" -fkqs $i in
	[ $? = 1 ] || test_failed $LINENO $i
	[ ! -e in.lz ] || test_failed $LINENO $i
done
"${LZIP}" -lq in
[ $? = 2 ] || test_failed $LINENO
"${LZIP}" -tq in
[ $? = 2 ] || test_failed $LINENO
"${LZIP}" -tq < in
[ $? = 2 ] || test_failed $LINENO
"${LZIP}" -cdq in
[ $? = 2 ] || test_failed $LINENO
"${LZIP}" -cdq < in
[ $? = 2 ] || test_failed $LINENO
"${LZIP}" -dq -o in < "${in_lz}"
[ $? = 1 ] || test_failed $LINENO
"${LZIP}" -dq -o in "${in_lz}"
[ $? = 1 ] || test_failed $LINENO
"${LZIP}" -dq -o out nx_file.lz
[ $? = 1 ] || test_failed $LINENO
[ ! -e out ] || test_failed $LINENO
"${LZIP}" -q -o out.lz nx_file
[ $? = 1 ] || test_failed $LINENO
[ ! -e out.lz ] || test_failed $LINENO
"${LZIP}" -qf -S100k -o out in in	# only one file with -o and -S
[ $? = 1 ] || test_failed $LINENO
{ [ ! -e out ] && [ ! -e out.lz ] ; } || test_failed $LINENO
# these are for code coverage
"${LZIP}" -lt "${in_lz}" 2> /dev/null
[ $? = 1 ] || test_failed $LINENO
"${LZIP}" -cdl "${in_lz}" 2> /dev/null
[ $? = 1 ] || test_failed $LINENO
"${LZIP}" -cdt "${in_lz}" 2> /dev/null
[ $? = 1 ] || test_failed $LINENO
"${LZIP}" -t -- nx_file.lz 2> /dev/null
[ $? = 1 ] || test_failed $LINENO
"${LZIP}" -t "" < /dev/null 2> /dev/null
[ $? = 1 ] || test_failed $LINENO
"${LZIP}" --help > /dev/null || test_failed $LINENO
"${LZIP}" -n1 -V > /dev/null || test_failed $LINENO
"${LZIP}" -m 2> /dev/null
[ $? = 1 ] || test_failed $LINENO
"${LZIP}" -z 2> /dev/null
[ $? = 1 ] || test_failed $LINENO
"${LZIP}" --bad_option 2> /dev/null
[ $? = 1 ] || test_failed $LINENO
"${LZIP}" --t 2> /dev/null
[ $? = 1 ] || test_failed $LINENO
"${LZIP}" --test=2 2> /dev/null
[ $? = 1 ] || test_failed $LINENO
"${LZIP}" --output= 2> /dev/null
[ $? = 1 ] || test_failed $LINENO
"${LZIP}" --output 2> /dev/null
[ $? = 1 ] || test_failed $LINENO
printf "LZIP\001-.............................." | "${LZIP}" -t 2> /dev/null
printf "LZIP\002-.............................." | "${LZIP}" -t 2> /dev/null
printf "LZIP\001+.............................." | "${LZIP}" -t 2> /dev/null

printf "\ntesting decompression..."

"${LZIP}" -l "${in_lz}" > /dev/null || test_failed $LINENO
"${LZIP}" -t "${in_lz}" || test_failed $LINENO
"${LZIP}" -d "${in_lz}" -o out || test_failed $LINENO
cmp in out || test_failed $LINENO
"${LZIP}" -cd "${in_lz}" > out || test_failed $LINENO
cmp in out || test_failed $LINENO
"${LZIP}" -d "${in_lz}" -o - > out || test_failed $LINENO
cmp in out || test_failed $LINENO
"${LZIP}" -d < "${in_lz}" > out || test_failed $LINENO
cmp in out || test_failed $LINENO
rm -f out || framework_failure

cp "${in_lz}" out.lz || framework_failure
"${LZIP}" -dk out.lz || test_failed $LINENO
cmp in out || test_failed $LINENO
rm -f out || framework_failure
"${LZIP}" -cd "${fox_lz}" > fox || test_failed $LINENO
cp fox copy || framework_failure
cp "${in_lz}" copy.lz || framework_failure
"${LZIP}" -d copy.lz out.lz 2> /dev/null	# skip copy, decompress out
[ $? = 1 ] || test_failed $LINENO
[ ! -e out.lz ] || test_failed $LINENO
cmp fox copy || test_failed $LINENO
cmp in out || test_failed $LINENO
"${LZIP}" -df copy.lz || test_failed $LINENO
[ ! -e copy.lz ] || test_failed $LINENO
cmp in copy || test_failed $LINENO
rm -f copy out || framework_failure

cp "${in_lz}" out.lz || framework_failure
"${LZIP}" -d -S100k out.lz || test_failed $LINENO	# ignore -S
[ ! -e out.lz ] || test_failed $LINENO
cmp in out || test_failed $LINENO

printf "to be overwritten" > out || framework_failure
"${LZIP}" -df -o out < "${in_lz}" || test_failed $LINENO
cmp in out || test_failed $LINENO
"${LZIP}" -d -o ./- "${in_lz}" || test_failed $LINENO
cmp in ./- || test_failed $LINENO
rm -f ./- || framework_failure
"${LZIP}" -d -o ./- < "${in_lz}" || test_failed $LINENO
cmp in ./- || test_failed $LINENO
rm -f ./- || framework_failure

cp "${in_lz}" anyothername || framework_failure
"${LZIP}" -dv - anyothername - < "${in_lz}" > out 2> /dev/null ||
	test_failed $LINENO
cmp in out || test_failed $LINENO
cmp in anyothername.out || test_failed $LINENO
rm -f anyothername.out || framework_failure

"${LZIP}" -lq in "${in_lz}"
[ $? = 2 ] || test_failed $LINENO
"${LZIP}" -lq nx_file.lz "${in_lz}"
[ $? = 1 ] || test_failed $LINENO
"${LZIP}" -tq in "${in_lz}"
[ $? = 2 ] || test_failed $LINENO
"${LZIP}" -tq nx_file.lz "${in_lz}"
[ $? = 1 ] || test_failed $LINENO
"${LZIP}" -cdq in "${in_lz}" > out
[ $? = 2 ] || test_failed $LINENO
cat out in | cmp in - || test_failed $LINENO		# out must be empty
"${LZIP}" -cdq nx_file.lz "${in_lz}" > out	# skip nx_file, decompress in
[ $? = 1 ] || test_failed $LINENO
cmp in out || test_failed $LINENO
rm -f out || framework_failure
cp "${in_lz}" out.lz || framework_failure
for i in 1 2 3 4 5 6 7 ; do
	printf "g" >> out.lz || framework_failure
	"${LZIP}" -alvv out.lz "${in_lz}" > /dev/null 2>&1
	[ $? = 2 ] || test_failed $LINENO $i
	"${LZIP}" -atvvvv out.lz "${in_lz}" 2> /dev/null
	[ $? = 2 ] || test_failed $LINENO $i
done
"${LZIP}" -dq in out.lz
[ $? = 2 ] || test_failed $LINENO
[ -e out.lz ] || test_failed $LINENO
[ ! -e out ] || test_failed $LINENO
[ ! -e in.out ] || test_failed $LINENO
"${LZIP}" -dq nx_file.lz out.lz
[ $? = 1 ] || test_failed $LINENO
[ ! -e out.lz ] || test_failed $LINENO
[ ! -e nx_file ] || test_failed $LINENO
cmp in out || test_failed $LINENO
rm -f out || framework_failure

cat in in > in2 || framework_failure
"${LZIP}" -l "${in_lz}" "${in_lz}" > /dev/null || test_failed $LINENO
"${LZIP}" -t "${in_lz}" "${in_lz}" || test_failed $LINENO
"${LZIP}" -cd "${in_lz}" "${in_lz}" -o out > out2 || test_failed $LINENO
[ ! -e out ] || test_failed $LINENO			# override -o
cmp in2 out2 || test_failed $LINENO
rm -f out2 || framework_failure
"${LZIP}" -d "${in_lz}" "${in_lz}" -o out2 || test_failed $LINENO
cmp in2 out2 || test_failed $LINENO
rm -f out2 || framework_failure

cat "${in_lz}" "${in_lz}" > out2.lz || framework_failure
lines=`"${LZIP}" -tvv out2.lz 2>&1 | wc -l` || test_failed $LINENO
[ "${lines}" -eq 2 ] || test_failed $LINENO "${lines}"
lines=`"${LZIP}" -lvv out2.lz | wc -l` || test_failed $LINENO
[ "${lines}" -eq 5 ] || test_failed $LINENO "${lines}"

printf "\ngarbage" >> out2.lz || framework_failure
"${LZIP}" -tvvvv out2.lz 2> /dev/null || test_failed $LINENO
"${LZIP}" -alq out2.lz
[ $? = 2 ] || test_failed $LINENO
"${LZIP}" -atq out2.lz
[ $? = 2 ] || test_failed $LINENO
"${LZIP}" -atq < out2.lz
[ $? = 2 ] || test_failed $LINENO
"${LZIP}" -adkq out2.lz
[ $? = 2 ] || test_failed $LINENO
[ ! -e out2 ] || test_failed $LINENO
"${LZIP}" -adkq -o out2 < out2.lz
[ $? = 2 ] || test_failed $LINENO
[ ! -e out2 ] || test_failed $LINENO
printf "to be overwritten" > out2 || framework_failure
"${LZIP}" -df out2.lz || test_failed $LINENO
cmp in2 out2 || test_failed $LINENO
rm -f out2 || framework_failure

"${LZIP}" -d "${fox_lz}" -o a/b/c/fox || test_failed $LINENO
cmp fox a/b/c/fox || test_failed $LINENO
rm -rf a || framework_failure
"${LZIP}" -d -o a/b/c/fox < "${fox_lz}" || test_failed $LINENO
cmp fox a/b/c/fox || test_failed $LINENO
rm -rf a || framework_failure
"${LZIP}" -dq "${fox_lz}" -o a/b/c/
[ $? = 1 ] || test_failed $LINENO
[ ! -e a ] || test_failed $LINENO

touch empty em || framework_failure
"${LZIP}" -0 em || test_failed $LINENO
"${LZIP}" -l em.lz > /dev/null || test_failed $LINENO
"${LZIP}" -dk em.lz || test_failed $LINENO
cmp empty em || test_failed $LINENO
cat em.lz em.lz | "${LZIP}" -t || test_failed $LINENO
cat em.lz em.lz | "${LZIP}" -d > em || test_failed $LINENO
cmp empty em || test_failed $LINENO
cat em.lz "${in_lz}" | "${LZIP}" -t || test_failed $LINENO
cat em.lz "${in_lz}" | "${LZIP}" -d > out || test_failed $LINENO
cmp in out || test_failed $LINENO
cat "${in_lz}" em.lz | "${LZIP}" -t || test_failed $LINENO
cat "${in_lz}" em.lz | "${LZIP}" -d > out || test_failed $LINENO
cmp in out || test_failed $LINENO

printf "\ntesting   compression..."

"${LZIP}" -c -0 in in in -S100k -o out3.lz > copy2.lz || test_failed $LINENO
[ ! -e out3.lz ] || test_failed $LINENO			# override -o and -S
"${LZIP}" -0f in in --output=copy2.lz || test_failed $LINENO
"${LZIP}" -d copy2.lz -o out2 || test_failed $LINENO
[ -e copy2.lz ] || test_failed $LINENO
cmp in2 out2 || test_failed $LINENO
rm -f copy2.lz || framework_failure

"${LZIP}" -cf "${in_lz}" > lzlz 2> /dev/null	# /dev/null is a tty on OS/2
[ $? = 1 ] || test_failed $LINENO
"${LZIP}" -Fvvm36 -o - "${in_lz}" > lzlz 2> /dev/null || test_failed $LINENO
"${LZIP}" -cd lzlz | "${LZIP}" -d > out || test_failed $LINENO
cmp in out || test_failed $LINENO
rm -f lzlz out || framework_failure

"${LZIP}" -0 -o ./- in || test_failed $LINENO
"${LZIP}" -cd ./- | cmp in - || test_failed $LINENO
rm -f ./- || framework_failure
"${LZIP}" -0 -o ./- < in || test_failed $LINENO			# add .lz
[ ! -e ./- ] || test_failed $LINENO
"${LZIP}" -cd -- -.lz | cmp in - || test_failed $LINENO
rm -f ./-.lz || framework_failure

for i in s4Ki 0 1 2 3 4 5 6 7 8 9 ; do
	"${LZIP}" -k -$i in || test_failed $LINENO $i
	mv in.lz out.lz || test_failed $LINENO $i
	printf "garbage" >> out.lz || framework_failure
	"${LZIP}" -df out.lz || test_failed $LINENO $i
	cmp in out || test_failed $LINENO $i

	"${LZIP}" -$i in -c > out || test_failed $LINENO $i
	"${LZIP}" -$i in -o o_out || test_failed $LINENO $i	# don't add .lz
	[ ! -e o_out.lz ] || test_failed $LINENO
	cmp out o_out || test_failed $LINENO $i
	rm -f o_out || framework_failure
	printf "g" >> out || framework_failure
	"${LZIP}" -cd out > copy || test_failed $LINENO $i
	cmp in copy || test_failed $LINENO $i

	"${LZIP}" -$i < in > out || test_failed $LINENO $i
	"${LZIP}" -d < out > copy || test_failed $LINENO $i
	cmp in copy || test_failed $LINENO $i

	rm -f out || framework_failure
	printf "to be overwritten" > out.lz || framework_failure
	"${LZIP}" -f -$i -o out < in || test_failed $LINENO $i	# add .lz
	[ ! -e out ] || test_failed $LINENO
	"${LZIP}" -df -o copy < out.lz || test_failed $LINENO $i
	cmp in copy || test_failed $LINENO $i
done
rm -f copy out.lz || framework_failure

cat in in in in in in in in > in8 || framework_failure
"${LZIP}" -1s12 -S100k in8 || test_failed $LINENO
"${LZIP}" -t in800001.lz in800002.lz || test_failed $LINENO
"${LZIP}" -cd in800001.lz in800002.lz | cmp in8 - || test_failed $LINENO
[ ! -e in800003.lz ] || test_failed $LINENO
rm -f in800001.lz in800002.lz || framework_failure
"${LZIP}" -1s12 -S100k -o out.lz in8 || test_failed $LINENO
# ignore -S
"${LZIP}" -d out.lz00001.lz out.lz00002.lz -S100k -o out || test_failed $LINENO
cmp in8 out || test_failed $LINENO
"${LZIP}" -t out.lz00001.lz out.lz00002.lz || test_failed $LINENO
[ ! -e out.lz00003.lz ] || test_failed $LINENO
rm -f out out.lz00001.lz out.lz00002.lz || framework_failure
"${LZIP}" -1ks4Ki -b100000 in8 || test_failed $LINENO
"${LZIP}" -t in8.lz || test_failed $LINENO
"${LZIP}" -cd in8.lz -o out | cmp in8 - || test_failed $LINENO	# override -o
[ ! -e out ] || test_failed $LINENO
rm -f in8 || framework_failure
"${LZIP}" -0 -S100k -o out < in8.lz || test_failed $LINENO
"${LZIP}" -t out00001.lz out00002.lz || test_failed $LINENO
"${LZIP}" -cd out00001.lz out00002.lz | cmp in8.lz - || test_failed $LINENO
[ ! -e out00003.lz ] || test_failed $LINENO
rm -f out00001.lz out00002.lz || framework_failure
"${LZIP}" -1 -S100k -o a/b/c/out < in8.lz || test_failed $LINENO
"${LZIP}" -t a/b/c/out00001.lz a/b/c/out00002.lz || test_failed $LINENO
"${LZIP}" -cd a/b/c/out00001.lz a/b/c/out00002.lz | cmp in8.lz - ||
	test_failed $LINENO
[ ! -e a/b/c/out00003.lz ] || test_failed $LINENO
rm -rf a || framework_failure
"${LZIP}" -0 -F -S100k in8.lz || test_failed $LINENO
"${LZIP}" -t in8.lz00001.lz in8.lz00002.lz || test_failed $LINENO
"${LZIP}" -cd in8.lz00001.lz in8.lz00002.lz | cmp in8.lz - || test_failed $LINENO
[ ! -e in8.lz00003.lz ] || test_failed $LINENO
rm -f in8.lz00001.lz in8.lz00002.lz || framework_failure
"${LZIP}" -0kF -b100k in8.lz || test_failed $LINENO
"${LZIP}" -t in8.lz.lz || test_failed $LINENO
"${LZIP}" -cd in8.lz.lz | cmp in8.lz - || test_failed $LINENO
rm -f in8.lz in8.lz.lz || framework_failure

"${LZIP}" fox -o a/b/c/fox.lz || test_failed $LINENO
cmp "${fox_lz}" a/b/c/fox.lz || test_failed $LINENO
rm -rf a || framework_failure
"${LZIP}" -o a/b/c/fox.lz < fox || test_failed $LINENO
cmp "${fox_lz}" a/b/c/fox.lz || test_failed $LINENO
rm -rf a || framework_failure

printf "\ntesting bad input..."

cat em.lz em.lz > ee.lz || framework_failure
"${LZIP}" -l < ee.lz > /dev/null || test_failed $LINENO
"${LZIP}" -t < ee.lz || test_failed $LINENO
"${LZIP}" -d < ee.lz > em || test_failed $LINENO
cmp empty em || test_failed $LINENO
"${LZIP}" -lq ee.lz
[ $? = 2 ] || test_failed $LINENO
"${LZIP}" -tq ee.lz
[ $? = 2 ] || test_failed $LINENO
"${LZIP}" -dq ee.lz
[ $? = 2 ] || test_failed $LINENO
[ ! -e ee ] || test_failed $LINENO
"${LZIP}" -cdq ee.lz > em
[ $? = 2 ] || test_failed $LINENO
cmp empty em || test_failed $LINENO
rm -f empty em || framework_failure
cat "${in_lz}" em.lz "${in_lz}" > inein.lz || framework_failure
"${LZIP}" -l < inein.lz > /dev/null || test_failed $LINENO
"${LZIP}" -t < inein.lz || test_failed $LINENO
"${LZIP}" -d < inein.lz > out2 || test_failed $LINENO
cmp in2 out2 || test_failed $LINENO
"${LZIP}" -lq inein.lz
[ $? = 2 ] || test_failed $LINENO
"${LZIP}" -tq inein.lz
[ $? = 2 ] || test_failed $LINENO
"${LZIP}" -dq inein.lz
[ $? = 2 ] || test_failed $LINENO
[ ! -e inein ] || test_failed $LINENO
"${LZIP}" -cdq inein.lz > out2
[ $? = 2 ] || test_failed $LINENO
cmp in2 out2 || test_failed $LINENO
rm -f in2 out2 inein.lz em.lz || framework_failure

headers='LZIp LZiP LZip LzIP LzIp LziP lZIP lZIp lZiP lzIP'
body='\001\014\000\000\101\376\367\377\377\340\000\200\000\215\357\002\322\001\000\000\000\000\000\000\000\045\000\000\000\000\000\000\000'
cp "${in_lz}" int.lz || framework_failure
printf "LZIP${body}" >> int.lz || framework_failure
if "${LZIP}" -t int.lz ; then
	for header in ${headers} ; do
		printf "${header}${body}" > int.lz || framework_failure
		"${LZIP}" -lq int.lz			# first member
		[ $? = 2 ] || test_failed $LINENO ${header}
		"${LZIP}" -tq int.lz
		[ $? = 2 ] || test_failed $LINENO ${header}
		"${LZIP}" -tq < int.lz
		[ $? = 2 ] || test_failed $LINENO ${header}
		"${LZIP}" -cdq int.lz > /dev/null
		[ $? = 2 ] || test_failed $LINENO ${header}
		"${LZIP}" -lq --loose-trailing int.lz
		[ $? = 2 ] || test_failed $LINENO ${header}
		"${LZIP}" -tq --loose-trailing int.lz
		[ $? = 2 ] || test_failed $LINENO ${header}
		"${LZIP}" -tq --loose-trailing < int.lz
		[ $? = 2 ] || test_failed $LINENO ${header}
		"${LZIP}" -cdq --loose-trailing int.lz > /dev/null
		[ $? = 2 ] || test_failed $LINENO ${header}
		cp "${in_lz}" int.lz || framework_failure
		printf "${header}${body}" >> int.lz || framework_failure
		"${LZIP}" -lq int.lz			# trailing data
		[ $? = 2 ] || test_failed $LINENO ${header}
		"${LZIP}" -tq int.lz
		[ $? = 2 ] || test_failed $LINENO ${header}
		"${LZIP}" -tq < int.lz
		[ $? = 2 ] || test_failed $LINENO ${header}
		"${LZIP}" -cdq int.lz > /dev/null
		[ $? = 2 ] || test_failed $LINENO ${header}
		"${LZIP}" -l --loose-trailing int.lz > /dev/null ||
			test_failed $LINENO ${header}
		"${LZIP}" -t --loose-trailing int.lz ||
			test_failed $LINENO ${header}
		"${LZIP}" -t --loose-trailing < int.lz ||
			test_failed $LINENO ${header}
		"${LZIP}" -cd --loose-trailing int.lz > /dev/null ||
			test_failed $LINENO ${header}
		"${LZIP}" -lq --loose-trailing --trailing-error int.lz
		[ $? = 2 ] || test_failed $LINENO ${header}
		"${LZIP}" -tq --loose-trailing --trailing-error int.lz
		[ $? = 2 ] || test_failed $LINENO ${header}
		"${LZIP}" -tq --loose-trailing --trailing-error < int.lz
		[ $? = 2 ] || test_failed $LINENO ${header}
		"${LZIP}" -cdq --loose-trailing --trailing-error int.lz > /dev/null
		[ $? = 2 ] || test_failed $LINENO ${header}
	done
else
	printf "warning: skipping header test: 'printf' does not work on your system."
fi
rm -f int.lz || framework_failure

"${LZIP}" -l "${fnz_lz}" > /dev/null || test_failed $LINENO
"${LZIP}" -tq "${fnz_lz}"
[ $? = 2 ] || test_failed $LINENO

for i in fox_v2.lz fox_s11.lz fox_de20.lz \
         fox_bcrc.lz fox_crc0.lz fox_das46.lz fox_mes81.lz ; do
	"${LZIP}" -tq "${testdir}"/$i
	[ $? = 2 ] || test_failed $LINENO $i
done

for i in fox_bcrc.lz fox_crc0.lz fox_das46.lz fox_mes81.lz ; do
	"${LZIP}" -cdq "${testdir}"/$i > out
	[ $? = 2 ] || test_failed $LINENO $i
	cmp fox out || test_failed $LINENO $i
done
rm -f fox || framework_failure

cat "${in_lz}" "${in_lz}" > in2.lz || framework_failure
cat "${in_lz}" "${in_lz}" "${in_lz}" > in3.lz || framework_failure
if dd if=in3.lz of=trunc.lz bs=14682 count=1 2> /dev/null &&
   [ -e trunc.lz ] && cmp in2.lz trunc.lz ; then
	for i in 6 20 14664 14683 14684 14685 14686 14687 14688 ; do
		dd if=in3.lz of=trunc.lz bs=$i count=1 2> /dev/null
		"${LZIP}" -lq trunc.lz
		[ $? = 2 ] || test_failed $LINENO $i
		"${LZIP}" -tq trunc.lz
		[ $? = 2 ] || test_failed $LINENO $i
		"${LZIP}" -tq < trunc.lz
		[ $? = 2 ] || test_failed $LINENO $i
		"${LZIP}" -cdq trunc.lz > /dev/null
		[ $? = 2 ] || test_failed $LINENO $i
		"${LZIP}" -dq < trunc.lz > /dev/null
		[ $? = 2 ] || test_failed $LINENO $i
	done
else
	printf "warning: skipping truncation test: 'dd' does not work on your system."
fi
rm -f in2.lz in3.lz trunc.lz || framework_failure

cp "${in_lz}" ingin.lz || framework_failure
printf "g" >> ingin.lz || framework_failure
cat "${in_lz}" >> ingin.lz || framework_failure
"${LZIP}" -lq ingin.lz
[ $? = 2 ] || test_failed $LINENO
"${LZIP}" -atq ingin.lz
[ $? = 2 ] || test_failed $LINENO
"${LZIP}" -atq < ingin.lz
[ $? = 2 ] || test_failed $LINENO
"${LZIP}" -acdq ingin.lz > out
[ $? = 2 ] || test_failed $LINENO
cmp in out || test_failed $LINENO
"${LZIP}" -adq < ingin.lz > out
[ $? = 2 ] || test_failed $LINENO
cmp in out || test_failed $LINENO
"${LZIP}" -t ingin.lz || test_failed $LINENO
"${LZIP}" -t < ingin.lz || test_failed $LINENO
"${LZIP}" -dk ingin.lz || test_failed $LINENO
cmp in ingin || test_failed $LINENO
"${LZIP}" -cd ingin.lz > out || test_failed $LINENO
cmp in out || test_failed $LINENO
"${LZIP}" -d < ingin.lz > out || test_failed $LINENO
cmp in out || test_failed $LINENO
rm -f out ingin ingin.lz || framework_failure

echo
if [ ${fail} = 0 ] ; then
	echo "tests completed successfully."
	cd "${objdir}" && rm -r tmp
else
	echo "tests failed."
fi
exit ${fail}
