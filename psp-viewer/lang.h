#ifndef LANG_H
#define LANG_H
#include <stddef.h>
#include <stdint.h>
/* The game's texts in the PSP's language (tools/convert_hud.py writes
 * lang_<xx>.n2lg from the disc's LANGUAGES tables): English, French,
 * German, Italian, Spanish or Dutch, the system's, or language.txt's. */
const char *lang_load(void);               /* its code ("en", "fr"...) */
const char *lang_code(void);
const char *lang_text(uint32_t label);     /* NULL: none, or English (the screens' own) */
const char *lang_or(uint32_t label,const char *english);   /* the language's, else the English given */
const char *lang_en(const char *english);  /* the language's for the game's English text, else that text
                                           * (a hint with the pad's icons: word by word between them) */
const char *lang_en_n(const char *english,size_t n);   /* the language's for those n bytes of English, NULL none */
int lang_format(char *b,size_t n,const char *english,const char *arg);   /* the language's "From: %s", arg in its %s (or %d) */
#endif
