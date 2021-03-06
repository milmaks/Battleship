#include "pch.h"
#include "Message.h"
#include <stdio.h>

#define _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_DEPRECATE
#define _CRT_NONSTDC_NO_DEPRECATE
#define _CRT_SECURE_CPP_OVERLOAD_STANDARD_NAMES 1
#define _CRT_SECURE_CPP_OVERLOAD_STANDARD_NAMES_COUNT 1

/*
	FormatMessageStruct
	-------------------------------------------------------------------
	FormatMessageStruct - omogucava da se formira poruka sa zeljenim parametrima
	MessageType type - vrsta poruke (enum)
	ActionPlayer player - igrac koji je u pitanju (enum)
	char arg1 - argumet korsiti se u zavisnmosti od konteksta pravljena poruke 
	char arg2 - argumet korsiti se u zavisnmosti od konteksta pravljena poruke 
	-------------------------------------------------------------------
	Povratna vrednost:
	Formirana poruka MESSAGE_TCP
*/
message FormatMessageStruct(MessageType type, ActionPlayer player, char arg1, char arg2) {
	message msg;

	msg.type = type;
	msg.player = player;
	msg.argument[0] = arg1;
	msg.argument[1] = arg2;
	msg.argument[2] = '/0';

	return msg;
}

/*
	FormatMessageStruct
	-------------------------------------------------------------------
	FormatMessageStruct - omogucava da se formira poruka sa zeljenim parametrima
	koristi se kada se zeli stati matrica
	MessageType type - vrsta poruke (enum)
	ActionPlayer player - igrac koji je u pitanju (enum)
	char matrix[10][10] - tabela sa brodovima
	-------------------------------------------------------------------
	Povratna vrednost:
	Formirana poruka MESSAGE_TCP
*/
message FormatMessageStruct(MessageType type, ActionPlayer player, char matrix[10][10]) {
	message msg;

	msg.type = type;
	msg.player = player;

	for (int i = 0; i < 10; i++)
	{
		for (int j = 0; j < 10; j++)
		{
			if (matrix[i][j] == 1) {
				//promasaj
				msg.matrixArgumetn[i][j] = 1;
			}
			else if (matrix[i][j] == 2) {
				//pogodak
				msg.matrixArgumetn[i][j] = 2;
			}
			else if (matrix[i][j] == 3) {
				//mesto broda
				msg.matrixArgumetn[i][j] = 3;
			}
			else {
				msg.matrixArgumetn[i][j] = 0;
			}
		}
	}

	return msg;
}

