# Overload inspector for C++

This is a fork of [LLVM project](https://github.com/llvm/llvm-project)

This tool helps for inspectiong and profileing overload resolutions in C++

Usage: After building clang from the code you can use it by calling the nerly built clang with adding the flag "-Xclang -ovins-dump" for the default configuration

If you want more controll you can use "-Xclang -ovins-dump-opt=..." where ... contains the additional settings separated by commas.

The valid options are (defaults highlited): 

CandName:($functionName) for filtering functions by name

($from)-($to) for filtering by line

[**Show**/Hide]NonViableCands for Showing/Hiding the non vable candidates

[Show/**Hide**]Includes for Showing/Hiding overvoads in heather files

[Show/**Hide**]EmptyOverloads for Showing/Hiding overloads without wiable or non viable candidates

[Show/**Hide**]BuiltInNonViable for Showing/Hiding the built in non viable operators

[Show/**Hide**]ImplicitConversions for Showing/Hiding the overload resolution for the implicit conversions

[**Show**/Hide]TemplateSpecs for Showing/Hiding whitch thmplate the type came from

[**Summarize**/Show]BuiltInBinOps for possibly summarizing the built in binary operators instead of listing all of them

[**Hide**/Summarize/Only]Time for profiling

[Hide/**Show**/Verbose]Compares for Showing/Hiding comparisons between candidates (Verbose shows even the less relevant ones)

[Hide/**Show**/Verbose]Conversions for Showing/Hiding the conversions needed for the candtdates

PrintYAML for printing YAML instead of remarks/notes
