/*
 * Stoper.c
 *
 * Created: 2019-01-01 23:40:18
 * Author : Miros�aw �ciebura
 */ 


#include <avr/io.h>
#include <util/delay.h>
#include <avr/interrupt.h>
#include "HD44780.h"

#define MIEDZYCZASY 8

volatile unsigned char tryb;	//zmienna okre�laj�ca tryb w kt�rym jest stoper
volatile unsigned char godziny; //bufor przechowuj�cy czas zliczony
volatile unsigned char minuty;
volatile unsigned char sekundy;
volatile unsigned int milisekundy;
volatile unsigned int mikrosekundy;
char znaki[13];	//string sta�ej d�ugo�ci dla wy�wietlania czasu
char historiaznaki[15]; //string sta�ej d�ugo�ci dla wy�wietlania kolejnych mi�dzyczas�w
unsigned char historiagodziny[MIEDZYCZASY];	//tablice dla zapamietywanych miedzyczas�w
unsigned char historiaminuty[MIEDZYCZASY];
unsigned char historiasekundy[MIEDZYCZASY];
unsigned int historiamilisekundy[MIEDZYCZASY];
unsigned int historiamikrosekundy[MIEDZYCZASY];
volatile unsigned char czywyswietlonostart; //blokada dla od�wie�ania startu
volatile unsigned char czywyswietlonostop; //blokada dla od�wie�ania stopu
volatile unsigned char pozycja; 
volatile unsigned char wyswietlanapozycja;
volatile unsigned char wyswietlaniemiedzyczasu;
volatile unsigned char odswiez;
volatile unsigned char bufor;

void uaktualnienie() //funkcja uaktualniaj�ca stan bufora czasu (inkrementacja liczby mikrosekund, milisekund)
{
	while (mikrosekundy > 999) //przepisywanie nadmiaru z ka�dej jednostki a� b�dzie poni�ej maksymalnej warto�ci
	{
		mikrosekundy -= 1000;
		milisekundy++;
	}
	while (milisekundy > 999)
	{
		milisekundy -= 1000;
		sekundy++;
		while (sekundy > 59)
		{
			sekundy -= 60;
			minuty++;
			while (minuty > 59)
			{
				minuty -= 60;
				godziny++;
				while (godziny > 99)
				{
					godziny = 0; //wyzerowanie zliczanego czasu - przekroczenie zakresu
					minuty = 0;
					sekundy = 0;
					milisekundy = 0;
					mikrosekundy = 0;
				}
			}
		}
	}
}

void czaspisemny() //wykonuje pisemn� reprezentacj� czasu w zmiennej globalnej znaki
{
	//milisekundy
	znaki[11] = 48 + (milisekundy % 10);
	znaki[10] = 48 + (((milisekundy - znaki[11] + 48) / 10) % 10);
	znaki[9] = 48 + ((milisekundy - milisekundy % 100) / 100);
	//sekundy
	znaki[7] = 48 + (sekundy % 10);
	znaki[6] = 48 + ((sekundy - znaki[7] + 48) / 10);
	//minuty
	znaki[4] = 48 + (minuty % 10);
	znaki[3] = 48 + ((minuty - znaki[4] + 48) / 10);
	//godziny
	znaki[1] = 48 + (godziny % 10); 
	znaki[0] = 48 + ((godziny - znaki[1] + 48) / 10);
}

void wlacztimer() //w��cza timer0 - ustawienie odpowiedniego prescalera wewn�trz
{
	//TCCR0 |= (1<<CS02) | (1<<CS00); //prescaler 1024
	TCCR0 |= (1<<CS02); //prescaler 256
	TIMSK |= (1<<TOIE0); //przerwanie przy przepe�nieniu
}

void wylacztimer() //w��cza timer0 - ustawia 000 w miejscu prescalera i zeruje bit overflow
{
	TCCR0 &= 0b11111000; //wy��czenie timera
	TIMSK &= ~(1<<TOIE0); // wy��cz przerwanie przy przepe�nieniu
}

void ekran(char tablicaznakow[], unsigned char poczatek, char* napis) //wypisuje na ekran LCD tablic� znak�w (c-string) w pierwszej linii od podanego znaku w niej i drugi napis w dolnej
{
	LCD_Clear(); //wyczy�� ekran
	LCD_GoTo(poczatek, 0); //przejd� do g�rnej linii na odpowiedni znak
	LCD_WriteText(tablicaznakow); //wypisz tekst
	LCD_GoTo(0,1); //id� do dolnej linii
	LCD_WriteText(napis); //wypisz dolny napis
}

void przepiszbufor() //przepisuje do bufora czasu informacj� z TCNT0 i zeruje ten rejestr
{
	bufor = TCNT0;
	TCNT0 = 0; //wyzeruj licznik timera0
	mikrosekundy += (bufor * 256) % 1000; //przepisz do odpowiednich zmiennych bufora (<262,144 ms)
	milisekundy += ((bufor * 256) - ((bufor * 256) % 1000)) /1000;
	uaktualnienie(); //"wyr�wnaj" dane w buforze
}

void wyswietlstart() //zeruje bufor i wy�wietla informacj� startow�
{
	godziny = 0; //wyzerowanie bufora
	minuty = 0;
	sekundy = 0;
	milisekundy = 0;
	mikrosekundy = 0;
	czaspisemny(); //generacja stringu z czasem do wy�wietlenia w tablicy znaki
	ekran(znaki, 2, "START    OSTATNI");
	czywyswietlonostart = 1;
}

void wyswietlstop() //oczyt czasu i stopuje
{
	czaspisemny(); //generacja stringu z czasem do wy�wietlenia w tablicy znaki
	ekran(znaki, 2, "KONTYNUUJ  RESET");
	czywyswietlonostop = 1;
}

void wyswietlmiedzyczas() //wpisuje miedzyczas na ekran LCD
{
	czaspisemny(); //generacja stringu z czasem do wy�wietlenia w tablicy znaki
	for (int i = 0; i < 13; i++)
		historiaznaki[i+2] = znaki[i]; //utworzenie stringa z indeksem na pocz�tku
	historiaznaki[0] = wyswietlanapozycja + 48; //po wpisie mi�dzyczasu wyswietlana pozycja jest "o jeden za ostatnim"
	historiaznaki[1] = '.';
	ekran(historiaznaki, 1, "NASTEPNY    WROC");
	wyswietlaniemiedzyczasu = 1;
}

void wpiszmiedzyczas() //�aduje do bufora odpowiedni czas z tablicy miedzyczas�w
{
	if (wyswietlanapozycja > pozycja) //je�eli na ustawionej pozycji nic nie by�o to wy�wietl z pierwszej
		wyswietlanapozycja = 0;
	godziny = historiagodziny[wyswietlanapozycja]; //przepisz czas do bufora
	minuty = historiaminuty[wyswietlanapozycja];
	sekundy = historiasekundy[wyswietlanapozycja];
	milisekundy = historiamilisekundy[wyswietlanapozycja];
	mikrosekundy = historiamikrosekundy[wyswietlanapozycja];
	wyswietlanapozycja++; //przejdz na nast�pn� pozycj� - pozwala te� na indeksowanie od 0
}

void wpiszdohistorii() //wpisuje do tablicy z histori� na aktualn� pozycj�
{
	historiagodziny[pozycja] = godziny; //przepisz czas na pozycj� do historii
	historiaminuty[pozycja] = minuty;
	historiasekundy[pozycja] = sekundy;
	historiamilisekundy[pozycja] = milisekundy;
	historiamikrosekundy[pozycja] = mikrosekundy;
}

void odnotujmiedzyczas() //wpisuje miedzyczas do tablicy (jak jest miejsce)
{
	if (pozycja < MIEDZYCZASY - 1) //je�eli jest miejsce - ostatnie musi by� puste dla ostatniego czasu
	{
		przepiszbufor();	//dodaj to co jest w timerze do bufora
		wpiszdohistorii(); //wpisz na pozycje pozycja do historii
		pozycja++; //przejdz na nast�pn� pozycje
	}
}

ISR(INT0_vect)
{
	przepiszbufor(); //przepisz to co w TCNT0
	switch (tryb)
	{
		case 0:
		//w��czenie timera
		TCNT0 = 0;
		wlacztimer();
		pozycja = 0; //stan poczatkowy do zapisu i wyswietlania
		wyswietlanapozycja = 1;
		tryb = 2;
		break;
		case 1:
		wpiszmiedzyczas();	//zapis miedzyczasu i potrzeba od�wie�enia
		wyswietlaniemiedzyczasu = 0;
		tryb = 1;
		break;
		case 2:
		wylacztimer();	//wy��czenie timera i przepisanie jego zawarto�ci
		przepiszbufor();
		czywyswietlonostop = 0; //potrzeba wy�wietlenia stopu
		tryb = 3;
		break;
		case 3:
		wlacztimer(); //ponowne uruchomienie timera po u�pieniu
		tryb = 2;
		break;
		default:
		break;
	}
	PORTB ^= (1<<PORTB0); //zapalenie i zgaszenie lamek - sprawdzani wciskania przycisku
	PORTB ^= (1<<PORTB1);
}

ISR(INT1_vect)
{
	przepiszbufor(); //przepisz to co w TCNT0
	switch (tryb)
	{
		case 0:
		wpiszmiedzyczas();	//wpis starych miedzyczasow do bufora i potrzeba wy�wietlenia
		wyswietlaniemiedzyczasu = 0;
		tryb = 1;
		break;
		case 1:
		wyswietlanapozycja = 0;	//przywr�cenie poczatkowego stanu wy�wietlania
		czywyswietlonostart = 0;	//informacja o potrzebie wy�wietlenia start
		tryb = 0;
		break;
		case 2:
		odnotujmiedzyczas(); //zapis miedzyczasu do tanlicy
		tryb = 2;
		break;
		case 3:
		wpiszdohistorii();	//wpisanie czasu ko�cowego do tablicy
		wyswietlanapozycja = 0; //wyj�ciowy stan wy�witlania
		czywyswietlonostart = 0; //informacja o potrzebie wy�wietlenia start
		tryb = 0;
		break;
		default:	//na ko�cu ka�dego przypadku w int0 lub int1 informacja o trybie w kt�rym b�dzie po zako�czeniu
		break;
	}
	PORTB ^= (1<<PORTB0); //zapalenie i zgaszenie lamek - sprawdzani wciskania przycisku
	PORTB ^= (1<<PORTB1);
}

ISR(TIMER0_OVF_vect)
{
	milisekundy += 65;
	mikrosekundy += 536; //1MHz prescaler 256
	uaktualnienie();	//uaktualnienie czasu bufora
}

ISR(TIMER2_OVF_vect)
{
	odswiez = 1;	//potrzeba od�wie�ania co ~33ms
}

int main(void)
{
	cli();
	 //Prze��czanie diody
	DDRB  |= (1<<DDRB0);
	DDRB  |= (1<<DDRB1);
	DDRD  &= ~(1<<DDRD2);
	PORTD |= (1<<PORTD2); //podci�gni�cie do VCC
	DDRD  &= ~(1<<DDRD3);
	PORTD |= (1<<PORTD3); //podci�gni�cie do VCC
	
	PORTB |= (1<<PORTB1);

	MCUCR = (MCUCR & 0b11110000) | 0b1010; //zbocze opadaj�ce na obu INT
	GICR |= 1<<INT0; //w��czenie przerwania INT0
	GICR |= 1<<INT1; //w��czenie przerwania INT0
	TCNT0 = 0;
	_delay_ms(50); //stabilizacja stan�w
	//znaki sta�e
	znaki[2] =':';
	znaki[5] =':';
	znaki[8] =':';
	znaki[12] = '\0';
	//stany wyj�ciowe flag
	pozycja = 0;
	czywyswietlonostart = 0;
	czywyswietlonostop = 0;
	wyswietlaniemiedzyczasu = 0;
	wyswietlanapozycja = 0;
	LCD_Initalize(); //inicjalizacja wy�wietlacza
	tryb = 0;
	odswiez = 0;
	TCNT2 = 0; //w��czenie timera 2 do sprawdzania czasu od�wie�ania p�tli
	TCCR2 |= (1<<CS22) | (1<<CS20); //prescaler 128
	TIMSK |= (1<<TOIE2);
	sei();
	while (1)
	{
		if (odswiez == 1) //czy potrzeba od�wie�y�
		{
			if (tryb == 0)
				if (czywyswietlonostart == 0) //czy jest co� nowego do wy�wietlenia
					wyswietlstart();
			if (tryb == 1)
				if (wyswietlaniemiedzyczasu == 0) //czy jest co� nowego do wy�wietlenia
					wyswietlmiedzyczas();
			if (tryb == 2)
			{
				czaspisemny(); //zbierz aktualny czas z bufora i odpowiednio wypisz
				ekran(znaki, 2, "STOP  MIEDZYCZAS");

			}
			if (tryb == 3)
			{
				if (czywyswietlonostop == 0) //czy jest co� nowego do wy�wietlenia
					wyswietlstop();
			}
			odswiez = 0;
		}
	}
}


