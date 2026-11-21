// 词典查询命令行测试入口。/ Dictionary lookup test driver.
#include "reader_dictionary.h"
#include <stdio.h>
#include <string.h>
int main(int argc, char **argv)
{
    static reader_dict_entry_t entry;
    if (argc != 3) return 1;
    reader_dict_status_t status = strcmp(argv[1], "-") == 0
        ? reader_dictionary_lookup(argv[2], &entry)
        : reader_dictionary_lookup_file(argv[1], argv[2], &entry);
    printf("%d\n%s\n%s\n%zu\n%d\n%s\n%s\n%s\n", (int)status, entry.word,
           entry.pronunciation, entry.count, entry.truncated, entry.senses[0].pos,
           entry.senses[0].gloss, entry.senses[0].synonyms);
    if (entry.source[0]) printf("%s\n", entry.source);
    for(size_t i=0;i<entry.count;i++) if(entry.senses[i].pronunciation[0]) printf("%s\n",entry.senses[i].pronunciation);
    return 0;
}
