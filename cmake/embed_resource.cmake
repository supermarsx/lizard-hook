file(READ "${INPUT}" data HEX)
string(REGEX REPLACE "\n" "" data "${data}")
string(REGEX REPLACE "(..)" "0x\\1," data "${data}")
file(WRITE "${OUTPUT}" "#include <cstddef>\nnamespace lizard::assets {\nconst unsigned char ${VAR}[] = {${data}};\nconst unsigned int ${VAR}_len = sizeof(${VAR});\n}\n")
