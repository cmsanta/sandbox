#!/bin/sh

FILE=$1
guestname=""
phone=""
email=""
checkin=""
checkout=""
totalguests=""
checkin_count=0
checkout_count=0
villa=0
language=""
outputfile=${1%.txt}.out

while read LINE; do
	oneline=$LINE
	#Look for Villa Name
	if [ `echo $oneline | grep -c "Rustica Beach House Property" ` -gt 0 ]
	then
		villa=830
		echo $oneline
	elif [ `echo $oneline | grep -c "Tortuga Beach Apartment Property" ` -gt 0 ]
	then
		villa=826
	elif [ `echo $oneline | grep -c "Acqualina Beach House Property" ` -gt 0 ]
	then
		villa=778
	elif [ `echo $oneline | grep -c "Solana Beach House Property" ` -gt 0 ]
	then
		villa=752
		echo $oneline
	#Look for Guest Name only in the line
	elif [ `echo $oneline | grep -c "Guest Name" ` -gt 0 ]
	then
		guestname=`echo $oneline | grep -Eo '[A-Za-zé]+'| tr '\n' '-\0'`
		guestname=${guestname##Guest-Name-}
	elif [ `echo $oneline | grep -c "Preferred language" ` -gt 0 ]
	then
		language=`echo $oneline | grep -Eo '[A-Za-z]+'| tr '\n' '-\0'`
		language=${language##Preferred-language-}
	#Phone Number
	elif [ `echo $oneline | grep -c "phone"` -gt 0 ]
	then
		#parse only numerals
		phone=`echo $oneline | grep -Eo '[0-9]{1,10}' | tr '\n' '\0'`
	#Email alias
	elif [ `echo $oneline | grep -c "@"` -gt 0 ]
	then
		email=$oneline
	#Total # of guests
	elif [ `echo $oneline | grep -c "Total guests" ` -gt 0 ]
	then
		totalguests=$oneline
		totalguests=${totalguests##Total guests:}
	#checkin time is actually found on the next line
	#use a counter to track that line
	elif [ `echo $oneline | grep -c "Check-in" ` -gt 0 ]
	then
		checkin_count=`expr $checkin_count + 1`
	#checkout time is actually found on the next line
	#use a counter to track that line
	elif [ `echo $oneline | grep -c "Check-out" ` -gt 0 ]
	then
		checkout_count=`expr $checkout_count + 1`
	#retrieve checkin line
	elif [ "$checkin_count" -eq 1 ]
	then
		checkin=$oneline
		checkin_count=`expr $checkin_count + 1`
	elif [ "$checkout_count" -eq 1 ]
	#retrieve checkout line
	then
		checkout=$oneline
		checkout_count=`expr $checkout_count + 1`
	fi

done < $FILE

	#echo "\nGuest Record from filename: $FILE"
	echo $villa
	echo "$villa" | tee -a $outputfile
	echo "$guestname" | tee -a $outputfile
	echo "${phone}" | tee -a $outputfile
	echo "$totalguests"  | tee -a $outputfile
	echo "$checkin" | tee -a $outputfile
	echo "$checkout" | tee -a $outputfile
	echo "$email"  | tee -a $outputfile
	echo "$language" | tee -a $outputfile
	echo -n "" > /dev/null




