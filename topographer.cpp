/* Let's start with a backstory, shall we?
 * I'm a huge fan of Steins;Gate and a couple of days ago
 * I fixed my good ol' PSP from 2009. Period.
 *
 * That's it, I just wanted to replay Steins;Gate on PSP, and
 * since there is no English patch, I decided to make my own.
 * After some googling I found the GitHub repo, but oh surprise-
 * surprise it was a deadend, since original author Riku didn't
 * leave any of his scripts for Cartographer to dump the
 * dialogs from the game's ISO.
 *
 * Long story short, after two days of more googling I've learnt
 * enough about the PSP ISO structure and "romhacking" in general,
 * so I decided to give it a shot.
 *
 * With more time I figured out how this nasty pointer table works,
 * so that I could use my own dialogs with lengths different from
 * the game's originals in Japanese.
 *
 * I was very happy at that moment cuz I truly felt like a hacker.
 *
 *
 *
 * Now onto the specs. Like I said, there are no original scripts
 * for Cartographer that I could use for dumping. So naturally I
 * had to use my own coding skilld to reinvent the wheel.
 *
 * Meet the Topographer.
 *
 * It really isn't as cool as Cartographer, BUT I'D SAY IT'S A LOT
 * EASIER, since I didn't get anywhere with Cartographer at all.
 *
 * Please note though, Topographer was made specifically for the
 * Steins;Gate PSP and cannot be used anywhere else
 *
 * Alright, the game data I'm interested in is located in
 * PSP_GAME/USRDIR/DATA0.AFS in the game ISO. That's an AFS archive,
 * structured by blocks of the length of 2048 bytes (lets denote one
 * block B. Then B = 2048 bytes).
 *
 * From now own I'm going to call "dialogs" as "strings", since not
 * everything I'm dumping is a dialog (some are names for PMFs to
 * play, and generally other "game controlling" codes).
 *
 * I suggest you to use AFSExplorer for this, since it makes it very
 * easy to, well, explore the AFS. Open DATA0.AFS and go to
 * DATA0.AFS/SCENE00.AFS/ That's where all the BIN files with strings
 * are located. You should note, that every file has a maximum size of
 * n*B, where n is a number of blocks. What can be seen here is that
 * none of the files have more than B bytes of free size, which suggests
 * that sizes are adjusted to fit data in the best way possible.
 *
 * Onto the BIN files themselves. I think of them as of two big Sections.
 * Section 1 is the part that is usualle referred to as "Pointer Table",
 * so that's how I'm calling it. Section 2 is a string section. Each
 * string is null-terminated (end in 0x00).
 *
 * To programmatically split the BIN file into two Sections I define
 * a splitAddress. It is the address right after of the last sequence
 * of bytes "0x0D 0x02". Everything before it, including the sequence
 * itself, lies in the Section 1. Everything after it, not including
 * the sequence itself, lies in Section 2.
 *
 * So how are strings mapped between both Sections? That's where the
 * available Atlas script from GitHub come in. They were enough to
 * decode a general pattern. We are interested in finding the corelation
 * of begging of string in Section 2 with how are they "called" in
 * Section 1.
 *
 * Lets define some terminology so it makes sense:
 * String Address is the address of the beginning of the string in Section 2.
 * Lookup Sequence is the sequence of flipped bytes of the address of the
 *   String Address.
 * Pointer Address is the address at which the Lookup Sequence is found.
 * Pointer Data / Pointer is the 4 bytes at the Pointer Address. Basically,
 *   it is the same as Lookup Sequence, but I like to differntiate between both.
 * Control Word is the replacement for the sequence of bytes when generating
 *   Atlas scripts. If a rule is "0000=<END2>", then "<END2>" is a control word
 * TODO: More terminology if needed
 *
 * Start with String Address (that'd be splitAddress or the one right after
 * a 0x00 of the previous string). Lets say the String Address is 0xABCD. Flip
 * the bytes around, basically changing endianness. It'll become 0xCDAB or a
 * sequence "0xCD 0xAB", that's the Lookup Sequence. Look for that sequence in
 * the BIN file. The address where that sequence is found is Pointer Address
 * used in Atlas scripts.
 *
 * So yeah, that's how the pointer tables are mapped in the Steins;Gate PSP.
 * Of course there are some special cases, like several 0x00 in a row,
 * which only indicate a slightly different type of the end of null-terminated
 * string. Don't worry about those, there are no pointers pointing to them.
 *
 * This program generates the Atlas files for all BIN files that can be
 * translated. Enjoy, I guess.
 *
 * Huh? What's that? Why is this code so bad and is all in one file? Well, to
 * be fair, it is easier to have it one file since it requires typing less in
 * the makefile. I already typed a lot. Just look at all this. My note of the
 * fact that I type a lot only adds to how much I type, huh. I should stop here.
 *
 *
 *
 * El Psy Kongroo.
 */

// Folder where the original BIN files are located
#define ORIGINAL_DATA_DIR "original-data"

// Folder to which output the Atlas scripts
#define ATLAS_OUT_DIR "patch-atlas"

// Table that raltes ShiftJIS and UTF8 together
#define RULE_TABLE "encoding.tbl"

#include <string>
#include <iostream>
#include <fstream>
#include <vector>
#include <algorithm>
#include <experimental/filesystem>
#include <termios.h>
#include <sstream>
#include <cctype>
#include <locale>
#include <unistd.h>
#include <iomanip>

using namespace std;
using namespace std::experimental::filesystem;

// Some utlity functions
uint32_t FlipBytes(uint32_t n);
bool SearchByteVectors(const vector<uint8_t> & searchIn,
    const vector<uint8_t> & searchFor, size_t pos);
void InsertNewLine(vector<uint8_t> & bytes, bool onlyOne = false);
void trim(string & s);

// List of bin files I'm interested in dumping
const vector<string> BIN_FILES = {
    "SG00_01", "SG01_01", "DATA",
};

// Replaces original bytes with replacement bytes
struct Rule
{
    vector<uint8_t> original;
    vector<uint8_t> replacement;
};

// Controls the rules that replace bytes based on the
//   encoding table
struct RuleTable
{
    vector<Rule> rules;

    void Load();
    void Sanitize(const vector<uint8_t> & bytes, vector<uint8_t> & output);
} ruleTable;

// Structure of the pointer. Take a look at the terminology above
struct Pointer
{
    // Pointer Address as defined in termilogy
    uint32_t address;

    // Pointer Data <=> Lookup Sequence as defined in termilogy
    uint32_t data;

    // String Address as defined in termilogy
    uint32_t stringAddress;

    // Data at String Address (the string from BIN file) that
    //   is adjusted for Atlas file generation
    vector<uint8_t> adjustedString;
};

// Structure for parsing and dumping the original BIN file
struct BinFile
{
    string binName = "original";
    uint32_t splitAddress = 0;

    vector<uint8_t> section1;
    vector<uint8_t> section2;
    vector<Pointer> pointerTable;

    uint32_t pointerLookupOffset= 0;

    void Process(const string & name);
    void Load(const string & filepath);
    void SplitSections();
    void MapPointers();
    void AdjustStrings();
    void PrintEndSequence();
    void WriteSection1(const string & filepath);
    void WriteAtlas(const string & filepath);

    uint32_t LookupPointer(uint32_t pointer);
};

void BinFile::Process(const string & name)
{
    binName = name;

    string filepath(string() + ORIGINAL_DATA_DIR + "/" + binName + ".BIN");
    cout << filepath << ":" << endl;

    Load(filepath);
    SplitSections();
    MapPointers();
    AdjustStrings();
    PrintEndSequence();

    filepath = string() + ATLAS_OUT_DIR + "/data-section1/" + binName + ".BIN";
    WriteSection1(filepath);

    filepath = string() + ATLAS_OUT_DIR + "/" + binName + ".txt";
    WriteAtlas(filepath);

    cout << endl;
}

void BinFile::Load(const string & filepath)
{
    cout << "\t...Loading BIN file" << endl;
    ifstream file(filepath, ios::binary);

    // Now we find the splitAddress. Looking at every pair of
    //   bytes to find the last appearance of the sequence "0x0D 0x02".
    // This will go through the entire file, so for now I also save
    //   the ENTIRE thing into Section 1.
    while(true)
    {
        char byteBuf[2];

        file.read(byteBuf, 2);

        if (file.eof())
        {
            break;
        }

        if ((uint8_t)byteBuf[0] == 0x0D &&
            (uint8_t)byteBuf[1] == 0x02)
        {
            splitAddress = file.tellg();
        }

        section1.push_back(byteBuf[0]);
        section1.push_back(byteBuf[1]);
    }

    file.close();

    cout << "\tOverall size is " << section1.size() << " bytes" << endl;
}

void BinFile::SplitSections()
{
    cout << "\t...Splitting into Sections" << endl;

    // Copy strings from the entirety of the BIN file in Section 1
    //   to Section 2
    section2 = vector<uint8_t>(
        section1.begin() + splitAddress,
        section1.end()
    );

    // Then leave only pointer table in Section 1
    section1.resize(splitAddress);

    cout << "\tSection 1 size is " << section1.size() << " bytes" << endl;
    cout << "\tSection 2 size is " << section2.size() << " bytes" << endl;
    cout << "\tSplit Address is 0x" << hex << uppercase
         << splitAddress << resetiosflags(cout.flags()) << endl;
}

void BinFile::MapPointers()
{
    cout << "\t...Mapping strings and pointers" << endl;

    // This offset is relative to the splitAddress, so that
    //   I can look for pointers
    uint32_t offset = 0;

    for(size_t i = 0; i < section2.size(); i++)
    {
        // Got to the end of string, do the lookup of the previously
        //   recorded offset+  splitAddress
        if (section2[i] == 0x00)
        {
            // Look for the address that contains current
            //  splitAddress + offset in the reverse order
            uint32_t stringAddress = splitAddress + offset;
            uint32_t lookupSequence = FlipBytes(stringAddress);
            uint32_t pointerAddress = LookupPointer(lookupSequence);

            // If pointer address exists then save it as a valid
            //   pointer
            if (pointerAddress != 0) {
                pointerTable.push_back({
                    pointerAddress,
                    lookupSequence,
                    stringAddress,
                });
            }

            offset = i + 1;
        }
    }

    cout << "\tTotal of " << pointerTable.size() << " pointers" << endl;
    cout << "\tString Address range from 0x"
         << hex << uppercase << pointerTable.front().stringAddress
         << nouppercase << " to 0x" << uppercase
         << pointerTable.back().stringAddress
         << resetiosflags(cout.flags()) << endl;
}

void BinFile::AdjustStrings()
{
    cout << "\t...Adjusting strings' encoding" << endl;

    size_t szPointers = pointerTable.size();
    for (size_t i = 0; i < szPointers; i++)
    {
        vector<uint8_t>::iterator endpoint;
        vector<uint8_t> str;

        // Get the endpoint of the string
        if (i + 1 < szPointers)
        {
            // If there are more strings after this one, then the endpoint
            //   would be right before the String Address
            endpoint = section2.begin() + pointerTable[i + 1].stringAddress - splitAddress;
        }
        else
        {
            // If there no other strings after this one, then just go to the
            //   end of the section
            endpoint = section2.end();
        }

        str = vector<uint8_t>(
            section2.begin() + pointerTable[i].stringAddress - splitAddress,
            endpoint
        );

        // Check if any of the raw bytes can be replaced by an UTF8 alternative
        // Or, in some cases, it actually replaces entire byte sequences with
        //   Control Words to display them correctly in the Atlas file
        ruleTable.Sanitize(str, pointerTable[i].adjustedString);
    }
}

void BinFile::PrintEndSequence()
{
    const vector<uint8_t> lastString = pointerTable.back().adjustedString;
    // TODO: TEMPORARY, REMOVE ONCE ALL <FILEEND> BYTE SEQUENCES ARE GATHERED
    cout << "END SEQUENCE: ";
    for(size_t i = 0; i < lastString.size(); i++)
    {
        cout << uppercase << hex << setfill('0') << setw(2) << (int)lastString[i] << resetiosflags(cout.flags());
    }
    cout << endl;
}

void BinFile::WriteSection1(const string & filepath)
{
    cout << "\t...Dumping Section 1" << endl;
    ofstream file(filepath, ios::out);

    // Just dump the entire Section 1
    file.write((char*)&section1[0], section1.size());

    file.close();
}

void BinFile::WriteAtlas(const string & filepath)
{
    cout << "\t...Writing" << endl;
    ofstream file(filepath, ios::binary);

    // Header of the Atlas file
    file << "#VAR(Table, TABLE)" << endl
         << "#ADDTBL(\"..\\encoding.tbl\", Table)" << endl
         << "#ACTIVETBL(Table)" << endl
         << "#VAR(PTR, CUSTOMPOINTER)" << endl
         << "#CREATEPTR(PTR, \"LINEAR\", 0, 32)" << endl
         << "#JMP($" << hex << uppercase << setfill('0') << setw(8)
            << splitAddress << ")" << endl
         << endl;

    // Write all pointers and strings
    for(size_t i = 0; i < pointerTable.size(); i++)
    {
        const Pointer & p = pointerTable[i];

        file << "#WRITE(PTR, $"
             << hex << uppercase << setfill('0') << setw(8)
                << p.address << ")" << (uint8_t)0x0D << (uint8_t)0x0A
             << dec << nouppercase;

        file << string(p.adjustedString.begin(), p.adjustedString.end())<< (uint8_t)0x0D << (uint8_t)0x0A;
    }

    file.close();
}

uint32_t BinFile::LookupPointer(uint32_t lookupSequence)
{
    // Look for the Lookup Sequence in Section 1. This is done
    //   with the step of 2 (i+=2) to omptimize the lookup, since
    //   all Pointer Addresses are even
    // Also using pointerLookupOffset to optimize the lookup.
    //   All pointer addresses are found in the ascending order, so
    //   it is safe to do so
    for(size_t i = pointerLookupOffset; i < section1.size(); i+=2)
    {
        // Check if four bytes are the same as the pointer address
        uint32_t pointerCheck =
            (section1[i] << 24) |
            (section1[i + 1] << 16) |
            (section1[i + 2] << 8) |
            (section1[i + 3] << 0);

        // Found the pointer, return its address
        if (pointerCheck == lookupSequence)
        {
            // Apply optimizations that any further lookups will
            //  be performed starting from this address
            pointerLookupOffset = i;
            return i;
        }
    }

    return 0;
}

void RuleTable::Load()
{
    cout << "Table " << RULE_TABLE << endl;

    ifstream file(RULE_TABLE);

    // Adding some rules manually here
    // For better formatting of @channel posts
    rules.push_back({
        { 0x5C, 0x6E }, // "\n"
        { 0x5C, 0x6E, 0x0D, 0x0A } // "\n" + actual new line
    });

    // Rule for spaces, since the spaces in the table get trimmed
    rules.push_back({
        { 0x20 },
        { ' ' }
    });

    // Go line-by-line and extract rules for the enconding table
    while (true)
    {
        string line;
        getline(file, line);

        if (file.eof())
        {
            break;
        }

        // Get rid of spaces/tabs/newlines
        trim(line);

        // Ignore empty lines
        if (line.empty())
        {
            continue;
        }

        // Ignore lines that don't have a rule
        size_t pos = line.find('=');
        if (pos == string::npos)
        {
            continue;
        }

        // Extract strings to the left and to the right of the '='
        string left(line, 0, pos);
        string right(line, pos + 1, line.size() - pos);

        // Decode the hex string into array of bytes
        vector<uint8_t> bytes;
        for (size_t i = 0; i < left.length(); i += 2)
        {
            uint8_t b = (uint8_t)strtol(left.substr(i, 2).c_str(), NULL, 16);
            bytes.push_back(b);
        }

        // Save the rule
        rules.push_back({
            bytes,
            vector<uint8_t>(right.begin(), right.end())
        });
    }

    file.close();
    cout << "Total rules: " << ruleTable.rules.size() << endl;
}

void RuleTable::Sanitize(const vector<uint8_t> & input, vector<uint8_t> & output)
{
    output.clear();

    // Go through the input bytes and check to see if a specific
    //   sequence can be converted from ShiftJIS to UTF-8 alternative
    //   based on the encoding table lookup
    for (size_t i = 0; i < input.size(); i++)
    {
        for(size_t j = 0; j < rules.size(); j++)
        {
            const Rule & r = rules[j];
            const size_t szReplacement = r.replacement.size();

            // If input string doesn't contain the sequence I'm
            //   looking for at this position then go onto the next rule
            if (!SearchByteVectors(input, r.original, i))
            {
                // If checking the last rule, but still no match found
                if (j == rules.size() - 1)
                {
                    // TODO: details
                    cout << "WARNING: Byte that cannot be replaced "
                    << hex << (int)input[i]
                    << resetiosflags(cout.flags()) << endl;
                }

                continue;
            }

            // Matching rule was found

            // Special case for the newline, which I want to have before every
            //   Control Word except for "<DICT>" and "</DICT>"
            if (szReplacement >= 2 &&
                r.replacement[szReplacement - 2] != 0x54 && // 'T'
                r.replacement[szReplacement - 1] == 0x3E)   // '>'
            {
                InsertNewLine(output, true);
            }

            // Add onto the adjusted string
            output.insert(
                output.end(),
                r.replacement.begin(),
                r.replacement.end()
            );

            // Another exception. Just to make it look nicer, I add
            //   a newline after each '】' so that character names in
            //   dialogs can be distinguished easier. Though for the DATA.BIN
            //   I don't want this to be a thing when '】' is followed by the
            //   "\n"
            const vector<uint8_t> BRACKET_CLOSE = { 0xE3, 0x80, 0x91 };
            if (SearchByteVectors(rules[j].replacement, BRACKET_CLOSE, 0) &&
                !SearchByteVectors(input, { 0x5C, 0x6E }, i + rules[j].original.size()))
            {
                InsertNewLine(output, true);
            }

            // Here I had to work with two byte arrays at once so there are
            //   no cases when the topographer would replace already replaced
            //   bytes. I iterate through the original sequnce of bytes
            //   by the number of bytes in the sequence I was looking for.
            //   -1 is present because I also increment i every loop cycle
            i += rules[j].original.size() - 1;

            // And yeah, once we replaced bytes at this position, don't
            //   replace them again, go check rules at new offset
            break;
        }
    }

    InsertNewLine(output);
}

int main()
{
    ruleTable.Load();

    // Remove any previous Atlas scripts
    remove_all(ATLAS_OUT_DIR);
    create_directory(ATLAS_OUT_DIR);

    // Create directory for storing BIN files that only have
    //   Section 1, so that Atlas then can use them to build
    //   final BINs
    create_directory(string() + ATLAS_OUT_DIR + "/data-section1");

    // Parse and dump the BIN files
    for (const auto & file : BIN_FILES)
    {
        BinFile bf;
        bf.Process(file);
    }

    return 0;
}

uint32_t FlipBytes(uint32_t n)
{
    uint32_t res = 0;

    // Bit trickery so that bytes reverse their order
    res = n & 0xFF;
    res = (res << 8) | ((n >> 8) & 0xFF);
    res = (res << 8) | ((n >> 16) & 0xFF);
    res = (res << 8) | ((n >> 24) & 0xFF);

    return res;
}

bool SearchByteVectors(const vector<uint8_t> & searchIn,
    const vector<uint8_t> & searchFor, size_t pos)
{
    // If searchFor doesn't fit in searchIn then I can just skip
    //   it, since it can never happen
    if (pos + searchFor.size() > searchIn.size())
    {
        return false;
    }

    // Perform a byte-by-byte search to check that searchFor can be
    //   found in searchIn
    for(size_t i = 0; i < searchFor.size(); i++)
    {
        if (searchIn[pos + i] != searchFor[i])
        {
            return false;
        }
    }

    return true;
}

void InsertNewLine(vector<uint8_t> & bytes, bool onlyOne)
{
    // If it doesn't matter how many newlines I can have
    //   then just insert it
    if (!onlyOne)
    {
        bytes.insert(bytes.end(), { 0x0D, 0x0A });
        return;
    }

    // Otherwise make sure that there was no newline before
    //   inserting this one
    if (bytes.size() >= 2 &&
        bytes[bytes.size() - 2] != 0x0D &&
        bytes[bytes.size() - 1] != 0x0A)
    {
        bytes.insert(bytes.end(), { 0x0D, 0x0A });
    }
}

void trim(string & s)
{
    // Remove any space characters from the string
    s.erase(s.begin(), find_if(s.begin(), s.end(), [](int c) {
        return !isspace(c);
    }));
    s.erase(find_if(s.rbegin(), s.rend(), [](int c) {
        return !isspace(c);
    }).base(), s.end());
}