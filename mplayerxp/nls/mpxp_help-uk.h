/* Translated by:  Volodymyr M. Lisivka <lvm@mystery.lviv.net>,
		   Andriy Gritsenko <andrej@lucky.net>*/
// UTF-8
#ifdef HELP_MPXP_DEFINE_STATIC
#ifndef MSGTR_BANNER_TEXT
static char* banner_text=
"\n\n"
"MPlayerXP " VERSION "(C) 2002 Nickols_K 2000-2002 Arpad Gereoffy (see DOCS!)\n"
"\n";

static char help_text[]=
"Запуск:   mplayerxp [опції] [path/]filename\n"
"\n"
"Опції:\n"
" -vo <drv[:dev]> вибір драйвера і пристрою відео виводу (список див. з '-vo help')\n"
" -ao <drv[:dev]> вибір драйвера і пристрою аудіо виводу (список див. з '-ao help')\n"
" -play.ss <час>  переміститися на задану (секунди або ГГ:ХХ:СС) позицію\n"
" -audio.off      без звуку\n"
" -video.fs       повноекранне програвання (повноекр.,зміна відео,масштабування\n"
" -sub.file <file>вказати файл субтитрів\n"
" -play.list<file>вказати playlist\n"
" -sync.framedrop дозволити втрату кадрів (для повільних машин)\n"
"\n"
"Клавіші:\n"
" <-  або ->      перемотування вперед/назад на 10 секунд\n"
" вверх або вниз  перемотування вперед/назад на  1 хвилину\n"
" pgup або pgdown перемотування вперед/назад на 10 хвилин\n"
" < або >         перемотування вперед/назад у списку програвання\n"
" p або ПРОБІЛ    зупинити фільм (будь-яка клавіша - продовжити)\n"
" q або ESC       зупинити відтворення і вихід\n"
" o               циклічний перебір OSD режимів:  нема / навігація / навігація+таймер\n"
" * або /         додати або зменшити гучність (натискання 'm' вибирає master/pcm)\n"
"\n"
" * * * ДЕТАЛЬНІШЕ ДИВ. ДОКУМЕНТАЦІЮ, ПРО ДОДАТКОВІ ОПЦІЇ І КЛЮЧІ! * * *\n"
"\n";
#endif
#endif