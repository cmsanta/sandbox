#!/bin/sh

FILE=$1
guestname=""
phone=""
checkin=""
checkout=""
totalguests=""
checkin_count=0
checkout_count=0
villa=0
outputfile=""

while read LINE; do
	oneline=$LINE
	#Look for Villa Name
	if [ `echo $oneline | grep -c "Rustica" ` -gt 0 ]
	then
		villa=830
	elif [ `echo $oneline | grep -c "Tortuga" ` -gt 0 ]
	then
		villa=826
	elif [ `echo $oneline | grep -c "Acqualina" ` -gt 0 ]
	then
		villa=778
	elif [ `echo $oneline | grep -c "Solana" ` -gt 0 ]
	then
		villa=752
	#Look for Guest Name only in the line
	elif [ `echo $oneline | grep -c "Guest Name" ` -gt 0 ]
	then
		guestname=`echo $oneline | grep -Eo '[A-Za-z]'|tr '\n' '\0'`
		guestname=${guestname##GuestName}
		outputfile=$guestname.txt
	#Phone Number
	elif [ `echo $oneline | grep -c "phone"` -gt 0 ]
	then
		#parse only numerals
		phone=`echo $oneline | grep -Eo '[0-9]{1,10}' | tr '\n' '\0'`
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

	echo "$villa" | tee -a $outputfile
	echo "$guestname" | tee -a $outputfile
	echo "${phone}" | tee -a $outputfile
	echo "$totalguests"  | tee -a $outputfile
	echo "$checkin" | tee -a $outputfile
	echo "$checkout" | tee -a $outputfile




