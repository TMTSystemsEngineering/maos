files=$(ls ../maos/*.[ch] ../lib/sys/*.[ch] ../lib/*.[ch] ../skyc/*.[ch] ../tools/*.[ch])

for file in $files;do
    if grep -q Copyright $file;then
	echo Stripping old Copyright from $file
	line=$(grep -m 1 -n www.gnu.org $file |cut -d ':' -f 1)
	echo line=$line
	line=$((line+1)) || continue
	sed -i "1,${line}d" $file || continue
    fi

    if ! grep -q Copyright $file; then
	echo will add to $file
	cat copy >>$file.new 
	echo     >>$file.new
	cat $file>>$file.new
	mv $file.new $file
    fi
done
