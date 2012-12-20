// Transated by:  Andreas Berntsen  <andreasb@kvarteret.org>
// Updated for 0.60 by: B. Johannessen <bob@well.com>
// UTF-8
// ========================= MPlayer hjelp ===========================

#ifdef HELP_MPXP_DEFINE_STATIC
#define MSGTR_BANNER_TEXT 1
static char* banner_text=
"\n\n"
"MPlayerXP " VERSION "2002 Nickols_K 2000-2002 Arpad Gereoffy (se DOCS!)\n"
"\n";

static char help_text[]=
"Bruk:    mplayerxp [valg] [sti/]filnavn\n"
"\n"
"Valg:\n"
" -vo <drv[:dev]> velg video-ut driver og enhet (se '-vo help' for liste)\n"
" -ao <drv[:dev]> velg lyd-ut driver og enhet (se '-ao help' for liste)\n"
" -play.ss <timepos>søk til gitt (sekunder eller hh:mm:ss) posisjon\n"
" -audio.off      ikke spill av lyd\n"
" -video.fs       fullskjerm avspillings valg (fullscr,vidmode chg,softw.scale)\n"
" -sub.file <fil> spesifiser hvilken subtitle fil som skal brukes\n"
" -sync.framedrop slå på bilde-dropping (for trege maskiner)\n"
"\n"
"Tastatur:\n"
" <- eller ->       søk bakover/fremover 10 sekunder\n"
" opp eller ned     søk bakover/fremover 1 minutt\n"
" < or >            søk bakover/fremover i playlisten\n"
" p eller MELLOMROM pause filmen (trykk en tast for å fortsette)\n"
" q eller ESC       stopp avspilling og avslutt programmet\n"
" o                 gå gjennom OSD modi:  ingen / søkelinje / søkelinje+tidsvisning\n"
" * eller /         øk eller mink volumet (trykk 'm' for å velge master/pcm)\n"
"\n"
" * * * SE PÅ MANSIDE FOR DETALJER, FLERE (AVANSERTE) VALG OG TASTER! * * *\n"
"\n";
#endif

// ========================= MPlayer messages ===========================

// mplayer.c:

#define MSGTR_Exiting "Avslutter"
#define MSGTR_Exit_frames "Antall forespurte bilder vist"
#define MSGTR_Exit_quit "Avslutt"
#define MSGTR_Exit_eof "Slutt på filen"
#define MSGTR_Fatal_error "Fatal feil"
#define MSGTR_NoHomeDir "Kan ikke finne HOME katalog"
#define MSGTR_Playing "Spiller"
