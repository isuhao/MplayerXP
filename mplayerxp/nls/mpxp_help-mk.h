// translated to Macedonian by: MIsTeRIoZ "Zoran Dimovski"<zoki@email.com>
// Last sync with help_mp-en.h 1.113
// UTF-8
#ifdef HELP_MPXP_DEFINE_STATIC
#ifndef MSGTR_BANNER_TEXT
static char* banner_text=
"\n\n"
"MPlayerXP " VERSION "(C) 2002 Nickols_K 2000-2002 Arpad Gereoffy (see DOCS!)\n"
"\n";

static char help_text[]=
"Употреба: mplayerxp [опции] [url|патека/]ИмеНаДатотеката\n"
"\n"
"Основни Опции: (комплетна листа на man страницата)\n"
" -vo <drv[:dev]>  избира излезен видео драјвер и уред ('-vo help' за листа)\n"
" -ao <drv[:dev]>  избира излезен аудио драјвер и уред ('-ao help' за листа)\n"
" -play.ss <timepos>бара до дадената (секунди или hh:mm:ss) позиција\n"
" -audio.off       не го пушта звукот\n"
" -video.fs        плејбек на цел екран (или -video.vm, -video.zoom, подетално во man страната)\n"
" -sub.file <file> одредува датотека со превод за употреба\n"
" -play.list <file>одредува датотека со плејлиста\n"
" -sync.framedrop  овозможува отфрлање на фрејмови (за слаби машини)\n"
"\n"
"Основни копчиња: (комплетна листа во man страната, проверете го исто така и input.conf)\n"
" <-  или  ->       бара назад/напред за 10 секунди\n"
" up или down       бара назад/напред за 1 минута\n"
" pgup или pgdown   бара назад/напред за 10 минути\n"
" < или >           чекор назад/напред во плејлистата\n"
" p или SPACE       го паузира филмот (притиснете на било кое копче да продолжи)\n"
" q или ESC         го стопира пуштањето и излегува од програмата\n"
" o                цикличен OSD мод: ниеден / барот за барање / барот за барање + тајмер\n"
" * или /           зголемување или намалување на PCM тонот\n"
"\n"
" * * * ВИДЕТЕ ЈА MAN СТРАНАТА ЗА ДЕТАЛИ, ПОВЕЌЕ (НАПРЕДНИ) ОПЦИИ И КОПЧИЊА* * *\n"
"\n";
#endif
#endif
