/* -*- C++ -*-
 *
 *  ScriptHandler.cpp - Script manipulation class
 *
 *  Copyright (c) 2001-2006 Ogapee (original ONScripter, of which this is a fork).
 *
 *  ogapee@aqua.dti2.ne.jp
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "ScriptHandler.h"
#include "utf8_util.h"

#define TMP_SCRIPT_BUF_LEN 4096
#define STRING_BUFFER_LENGTH 2048

#define SKIP_SPACE(p) while ( *(p) == ' ' || *(p) == '\t' ) (p)++

ScriptHandler::ScriptHandler()
{
	num_of_labels = 0;
	script_buffer = NULL;
	kidoku_buffer = NULL;
	log_info[LABEL_LOG].filename = "NScrllog.dat";
	log_info[FILE_LOG].filename  = "NScrflog.dat";
	clickstr_list = NULL;

	string_buffer       = new char[STRING_BUFFER_LENGTH];
	str_string_buffer   = new char[STRING_BUFFER_LENGTH];
	saved_string_buffer = new char[STRING_BUFFER_LENGTH];

	variable_data = new VariableData[VARIABLE_RANGE+1]; // the last one is a sink
	root_array_variable = NULL;

	screen_size = SCREEN_SIZE_640x480;
	global_variable_border = 200;
	
	save_path = NULL;
	game_identifier = NULL;
	script_defined_font = NULL;
}

ScriptHandler::~ScriptHandler()
{
	reset();

	if ( script_buffer ) delete[] script_buffer;
	if ( kidoku_buffer ) delete[] kidoku_buffer;

	delete[] string_buffer;
	delete[] string_buffer;
	delete[] saved_string_buffer;
	delete[] variable_data;
	
	if (game_identifier) delete[] game_identifier;
	if (script_defined_font) delete[] script_defined_font;
}

void ScriptHandler::reset()
{
	for (int i=0 ; i<VARIABLE_RANGE ; i++)
		variable_data[i].reset(true);

	ArrayVariable *av = root_array_variable;
	while(av){
		ArrayVariable *tmp = av;
		av = av->next;
		delete tmp;
	}
	root_array_variable = current_array_variable = NULL;

	// reset log info
	resetLog( log_info[LABEL_LOG] );
	resetLog( log_info[FILE_LOG] );

	// reset number alias
	Alias *alias;
	alias = root_num_alias.next;
	while (alias){
		Alias *tmp = alias;
		alias = alias->next;
		delete tmp;
	};
	last_num_alias = &root_num_alias;
	last_num_alias->next = NULL;

	// reset string alias
	alias = root_str_alias.next;
	while (alias){
		Alias *tmp = alias;
		alias = alias->next;
		delete tmp;
	};
	last_str_alias = &root_str_alias;
	last_str_alias->next = NULL;

	// reset misc. variables
	end_status = END_NONE;
	kidokuskip_flag = false;
	text_flag = true;
	linepage_flag = false;
	textgosub_flag = false;
	skip_enabled = false;
	if (clickstr_list){
		delete[] clickstr_list;
		clickstr_list = NULL;
	}

	default_encoding = 'r';
}

FILE *ScriptHandler::fopen( const char *path, const char *mode, const bool save )
{
	const char* root = save ? save_path : archive_path;
	char * file_name = new char[strlen(root)+strlen(path)+1];
	sprintf( file_name, "%s%s", root, path );

	FILE *fp = ::fopen( file_name, mode );
	delete[] file_name;

	return fp;
}

void ScriptHandler::setKeyTable( const unsigned char *key_table )
{
	int i;
	if (key_table){
		key_table_flag = true;
		for (i=0 ; i<256 ; i++) this->key_table[i] = key_table[i];
	}
	else{
		key_table_flag = false;
		for (i=0 ; i<256 ; i++) this->key_table[i] = i;
	}
}

// basic parser function
const char *ScriptHandler::readToken()
{
	current_script = next_script;
	char *buf = current_script;
	end_status = END_NONE;
	current_variable.type = VAR_NONE;

	text_flag = false;

	SKIP_SPACE( buf );
	markAsKidoku( buf );

  readTokenTop:
	string_counter = 0;
	char ch = *buf;
	if (ch == ';'){ // comment
		addStringBuffer( ch );
		do{
			ch = *++buf;
			addStringBuffer( ch );
		} while ( ch != 0x0a && ch != '\0' );
	}
	else if (ch & 0x80 ||
			 (ch >= '0' && ch <= '9') ||
			 ch == '@' || ch == '\\' || ch == '/' ||
			 ch == '%' || ch == '?' || ch == '$' ||
			 ch == '[' || ch == '(' ||
			 ch == '!' || ch == '#' || ch == ',' || ch == '"'){ // text
		bool loop_flag = true;
		bool ignore_click_flag = false;
		do{
			char bytes = CharacterBytes(&ch);
			if (bytes > 1) {
				if ( textgosub_flag && !ignore_click_flag && checkClickstr(buf) > 0) loop_flag = false;
				while (bytes--) addStringBuffer(*buf++);
				SKIP_SPACE(buf);
				ch = *buf;
			}
			else{
				if (ch == '%' || ch == '?'){
					addIntVariable(&buf);
				}
				else if (ch == '$'){
					addStrVariable(&buf);
				}
				else{
					if (textgosub_flag && !ignore_click_flag && checkClickstr(buf) == 1)
						loop_flag = false;
					addStringBuffer( ch );
					buf++;
					ignore_click_flag = false;
					if (ch == '_') ignore_click_flag = true;
				}
				if (ch>='0' && ch<='9' && (*buf == ' ' || *buf == '\t'
					|| *buf == '`'
					 ) && string_counter%2 == 1) addStringBuffer( ' ' );
				ch = *buf;
				if (ch == 0x0a || ch == '\0' || !loop_flag
					|| ch == '`'
					) break;
				SKIP_SPACE(buf);
				ch = *buf;
			}
		}
		while (ch != 0x0a && ch != '\0' && loop_flag
			   && ch != '`'
			   );
		if (loop_flag && ch == 0x0a && !(textgosub_flag && linepage_flag)){
			addStringBuffer( ch );
			markAsKidoku( buf++ );
		}

		text_flag = true;
	}
	else if (ch == '`'){
		ch = *++buf;
		char encoding = default_encoding;
		while (ch != '`' && ch != 0x0a && ch !='\0') {
			if ((ch == '\\' || ch == '@') && (buf[1] == 0x0a || buf[1] == 0)) {
				addStringBuffer(*buf++);
				ch = *buf;
				break;
			}
			if (ch == '~' && (ch = *++buf) != '~') {
				encoding = ch;
				ch = *++buf;
				if (ch == '~') ch = *++buf;
				continue;
			}
			if (encoding != 'r') {
				const unsigned short uc = UnicodeOfUTF8(buf);
				buf += CharacterBytes(&ch);
				char b2[5];
				UTF8OfUnicode(get_encoded_char(encoding, uc), b2);
				for (int i = 0; b2[i]; ++i) addStringBuffer(b2[i]);
			}
			else {
				char bytes = CharacterBytes(&ch);
				while (bytes--) addStringBuffer(*buf++);
			}
			ch = *buf;
		}
		if (ch == '`') ++buf;
		if (ch == 0x0a && !(textgosub_flag && linepage_flag)){
			addStringBuffer( ch );
			markAsKidoku( buf++ );
		}

		text_flag = true;
		end_status |= END_1BYTE_CHAR;
	}
	else if ((ch >= 'a' && ch <= 'z') ||
			 (ch >= 'A' && ch <= 'Z') ||
			 ch == '_'){ // command
		do{
			if (ch >= 'A' && ch <= 'Z') ch += 'a' - 'A';
			addStringBuffer( ch );
			ch = *++buf;
		}
		while((ch >= 'a' && ch <= 'z') ||
			  (ch >= 'A' && ch <= 'Z') ||
			  (ch >= '0' && ch <= '9') ||
			  ch == '_');
	}
	else if (ch == '*'){ // label
		return readLabel();
	}
	else if (ch == '~' || ch == 0x0a || ch == ':'){
		addStringBuffer( ch );
		markAsKidoku( buf++ );
	}
	else if (ch != '\0'){
		fprintf(stderr, "readToken: skip unknown heading character %c (%x)\n", ch, ch);
		buf++;
		goto readTokenTop;
	}

	next_script = checkComma(buf);

	//printf("readToken [%s] len=%d [%c(%x)] %p\n", string_buffer, strlen(string_buffer), ch, ch, next_script);

	return string_buffer;
}

const char *ScriptHandler::readLabel()
{
	end_status = END_NONE;
	current_variable.type = VAR_NONE;

	current_script = next_script;
	SKIP_SPACE( current_script );
	char *buf = current_script;

	string_counter = 0;
	char ch = *buf;
	if (ch == '$'){
		addStrVariable(&buf);
	}
	else if ((ch >= 'a' && ch <= 'z') ||
		(ch >= 'A' && ch <= 'Z') ||
		ch == '_' || ch == '*'){
		if (ch >= 'A' && ch <= 'Z') ch += 'a' - 'A';
		addStringBuffer( ch );
		buf++;
		if (ch == '*') SKIP_SPACE(buf);

		ch = *buf;
		while((ch >= 'a' && ch <= 'z') ||
			  (ch >= 'A' && ch <= 'Z') ||
			  (ch >= '0' && ch <= '9') ||
			  ch == '_'){
			if (ch >= 'A' && ch <= 'Z') ch += 'a' - 'A';
			addStringBuffer( ch );
			ch = *++buf;
		}
	}
	addStringBuffer( '\0' );

	next_script = checkComma(buf);

	return string_buffer;
}

const char *ScriptHandler::readStr()
{
	end_status = END_NONE;
	current_variable.type = VAR_NONE;

	current_script = next_script;
	SKIP_SPACE( current_script );
	char *buf = current_script;

	string_buffer[0] = '\0';
	string_counter = 0;

	while(1){
		parseStr(&buf);
		buf = checkComma(buf);
		string_counter += strlen(string_buffer);
		if (string_counter+1 >= STRING_BUFFER_LENGTH)
			errorAndExit("readStr: string length exceeds 2048 bytes.");
		strcat(string_buffer, str_string_buffer);
		if (buf[0] != '+') break;
		buf++;
	}
	next_script = buf;

	return string_buffer;
}

int ScriptHandler::readInt()
{
	string_counter = 0;
	string_buffer[string_counter] = '\0';

	end_status = END_NONE;
	current_variable.type = VAR_NONE;

	current_script = next_script;
	SKIP_SPACE( current_script );
	char *buf = current_script;

	int ret = parseIntExpression(&buf);

	next_script = checkComma(buf);

	return ret;
}

void ScriptHandler::skipToken()
{
	current_script = next_script;
	SKIP_SPACE( current_script );
	char *buf = current_script;

	bool quat_flag = false;
	bool text_flag = false;
	while(1){
		if ( *buf == 0x0a ||
			 (!quat_flag && !text_flag && (*buf == ':' || *buf == ';') ) ) break;
		if ( *buf == '"' ) quat_flag = !quat_flag;
		const char bytes = CharacterBytes(buf);
		if ( bytes > 1 && !quat_flag ) text_flag = true;
		buf += bytes;
	}
	next_script = buf;
}

// string access function
char *ScriptHandler::saveStringBuffer()
{
	strcpy( saved_string_buffer, string_buffer );
	return saved_string_buffer;
}

// script address direct manipulation function
void ScriptHandler::setCurrent(char *pos)
{
	current_script = next_script = pos;
}

void ScriptHandler::pushCurrent( char *pos )
{
	pushed_current_script = current_script;
	pushed_next_script = next_script;

	current_script = pos;
	next_script = pos;
}

void ScriptHandler::popCurrent()
{
	current_script = pushed_current_script;
	next_script = pushed_next_script;
}

int ScriptHandler::getOffset( char *pos )
{
	return pos - script_buffer;
}

char *ScriptHandler::getAddress( int offset )
{
	return script_buffer + offset;
}

int ScriptHandler::getLineByAddress( char *address )
{
	LabelInfo label = getLabelByAddress( address );

	char *addr = label.label_header;
	int line = 0;
	while ( address > addr ){
		if ( *addr == 0x0a ) line++;
		addr++;
	}
	return line;
}

char *ScriptHandler::getAddressByLine( int line )
{
	LabelInfo label = getLabelByLine( line );

	int l = line - label.start_line;
	char *addr = label.label_header;
	while ( l > 0 ){
		while( *addr != 0x0a ) addr++;
		addr++;
		l--;
	}
	return addr;
}

ScriptHandler::LabelInfo ScriptHandler::getLabelByAddress( char *address )
{
	int i;
	for ( i=0 ; i<num_of_labels-1 ; i++ ){
		if ( label_info[i+1].start_address > address )
			return label_info[i];
	}
	return label_info[i];
}

ScriptHandler::LabelInfo ScriptHandler::getLabelByLine( int line )
{
	int i;
	for ( i=0 ; i<num_of_labels-1 ; i++ ){
		if ( label_info[i+1].start_line > line )
			return label_info[i];
	}
	return label_info[i];
}

bool ScriptHandler::isName( const char *name )
{
	return (strncmp( name, string_buffer, strlen(name) )==0)?true:false;
}

bool ScriptHandler::isText()
{
	return text_flag;
}

bool ScriptHandler::compareString(const char *buf)
{
	SKIP_SPACE(next_script);
	unsigned int i, num = strlen(buf);
	for (i=0 ; i<num ; i++){
		unsigned char ch = next_script[i];
		if ('A' <= ch && 'Z' >= ch) ch += 'a' - 'A';
		if (ch != buf[i]) break;
	}
	return (i==num)?true:false;
}

void ScriptHandler::skipLine( int no )
{
	for ( int i=0 ; i<no ; i++ ){
		while ( *current_script != 0x0a ) current_script++;
		current_script++;
	}
	next_script = current_script;
}

void ScriptHandler::setLinepage( bool val )
{
	linepage_flag = val;
}

// function for kidoku history
bool ScriptHandler::isKidoku()
{
	return skip_enabled;
}

void ScriptHandler::markAsKidoku( char *address )
{
	if ( !kidokuskip_flag ) return;
	int offset = current_script - script_buffer;
	if ( address ) offset = address - script_buffer;
	//printf("mark (%c)%x:%x = %d\n", *current_script, offset /8, offset%8, kidoku_buffer[ offset/8 ] & ((char)1 << (offset % 8)));
	if ( kidoku_buffer[ offset/8 ] & ((char)1 << (offset % 8)) )
		skip_enabled = true;
	else
		skip_enabled = false;
	kidoku_buffer[ offset/8 ] |= ((char)1 << (offset % 8));
}

void ScriptHandler::setKidokuskip( bool kidokuskip_flag )
{
	this->kidokuskip_flag = kidokuskip_flag;
}

void ScriptHandler::saveKidokuData()
{
	FILE *fp;

	if ( ( fp = fopen( "kidoku.dat", "wb", true ) ) == NULL ){
		fprintf( stderr, "can't write kidoku.dat\n" );
		return;
	}

	fwrite( kidoku_buffer, 1, script_buffer_length/8, fp );
	fclose( fp );
}

void ScriptHandler::loadKidokuData()
{
	FILE *fp;

	setKidokuskip( true );
	kidoku_buffer = new char[ script_buffer_length/8 + 1 ];
	memset( kidoku_buffer, 0, script_buffer_length/8 + 1 );

	if ( ( fp = fopen( "kidoku.dat", "rb", true ) ) != NULL ){
		fread( kidoku_buffer, 1, script_buffer_length/8, fp );
		fclose( fp );
	}
}

void ScriptHandler::addIntVariable(char **buf)
{
	char num_buf[20];
	int no = parseInt(buf);

	int len = getStringFromInteger( num_buf, no, -1 );
	for (int i=0 ; i<len ; i++)
		addStringBuffer( num_buf[i] );
}

void ScriptHandler::addStrVariable(char **buf)
{
	(*buf)++;
	int no = parseInt(buf);
	VariableData &vd = variable_data[no];
	if ( vd.str ){
		for (unsigned int i=0 ; i<strlen( vd.str ) ; i++){
			addStringBuffer( vd.str[i] );
		}
	}
}

void ScriptHandler::enableTextgosub(bool val)
{
	textgosub_flag = val;
}

void ScriptHandler::setClickstr(const char *list)
{
	if (clickstr_list) delete[] clickstr_list;
	clickstr_list = new char[strlen(list)+2];
	memcpy( clickstr_list, list, strlen(list)+1 );
	clickstr_list[strlen(list)+1] = '\0';
}

int ScriptHandler::checkClickstr(const char *buf, bool recursive_flag)
{
	if ( buf[0] == '@' || buf[0] == '\\' ) return 1;

	if (clickstr_list == NULL) return 0;

	//bool double_byte_check = true;
	char *click_buf = clickstr_list;
	while(click_buf[0]){
		if (click_buf[0] == '`'){
			click_buf++;
			//double_byte_check = false;
			continue;
		}
		//if (double_byte_check){
		//	if ( click_buf[0] == buf[0] && click_buf[1] == buf[1] ){
		//		if (!recursive_flag && checkClickstr(buf+2, true) > 0) return 0;
		//		return 2;
		//	}
		//	click_buf += 2;
		//}
		//else{
		//	if ( click_buf[0] == buf[0] ){
		//		if (!recursive_flag && checkClickstr(buf+1, true) > 0) return 0;
		//		return 1;
		//	}
		//	click_buf++;
		//}
		char bytes = CharacterBytes(click_buf);
		bool match = true;
		for (int i = 0; i < bytes; ++i) match &= click_buf[i] == buf[i];
		if (match) {
			if (!recursive_flag && checkClickstr(buf + bytes, true) > 0) return 0;
			return bytes;
		}
	}

	return 0;
}

int ScriptHandler::getIntVariable( VariableInfo *var_info )
{
	if ( var_info == NULL ) var_info = &current_variable;

	if ( var_info->type == VAR_INT )
		return variable_data[var_info->var_no].num;
	else if ( var_info->type == VAR_ARRAY )
		return *getArrayPtr( var_info->var_no, var_info->array, 0 );
	return 0;
}

void ScriptHandler::readVariable( bool reread_flag )
{
	end_status = END_NONE;
	current_variable.type = VAR_NONE;
	if ( reread_flag ) next_script = current_script;
	current_script = next_script;
	char *buf = current_script;

	SKIP_SPACE(buf);

	bool ptr_flag = false;
	if ( *buf == 'i' || *buf == 'f' ){
		ptr_flag = true;
		buf++;
	}

	if ( *buf == '%' ){
		buf++;
		current_variable.var_no = parseInt(&buf);
		if (current_variable.var_no < 0 ||
			current_variable.var_no >= VARIABLE_RANGE)
			current_variable.var_no = VARIABLE_RANGE;
		current_variable.type = VAR_INT;
	}
	else if ( *buf == '?' ){
		ArrayVariable av;
		current_variable.var_no = parseArray( &buf, av );
		current_variable.type = VAR_ARRAY;
		current_variable.array = av;
	}
	else if ( *buf == '$' ){
		buf++;
		current_variable.var_no = parseInt(&buf);
		if (current_variable.var_no < 0 ||
			current_variable.var_no >= VARIABLE_RANGE)
			current_variable.var_no = VARIABLE_RANGE;
		current_variable.type = VAR_STR;
	}

	if (ptr_flag) current_variable.type |= VAR_PTR;

	next_script = checkComma(buf);
}

void ScriptHandler::setInt( VariableInfo *var_info, int val, int offset )
{
	if ( var_info->type & VAR_INT ){
		setNumVariable( var_info->var_no + offset, val );
	}
	else if ( var_info->type & VAR_ARRAY ){
		*getArrayPtr( var_info->var_no, var_info->array, offset ) = val;
	}
	else{
		errorAndExit( "setInt: no variables." );
	}
}

void ScriptHandler::pushVariable()
{
	pushed_variable = current_variable;
}

void ScriptHandler::setNumVariable( int no, int val )
{
	if ( no < 0 || no >= VARIABLE_RANGE )
		no = VARIABLE_RANGE;

	VariableData &vd = variable_data[no];
	if ( vd.num_limit_flag ){
		if      ( val < vd.num_limit_lower ) val = vd.num;
		else if ( val > vd.num_limit_upper ) val = vd.num;
	}
	vd.num = val;
}

int ScriptHandler::getStringFromInteger( char *buffer, int no, int num_column, bool is_zero_inserted )
{
	int i, num_space=0, num_minus = 0;
	if (no < 0){
		num_minus = 1;
		no = -no;
	}
	int num_digit=1, no2 = no;
	while(no2 >= 10){
		no2 /= 10;
		num_digit++;
	}

	if (num_column < 0) num_column = num_digit+num_minus;
	if (num_digit+num_minus <= num_column)
		num_space = num_column - (num_digit+num_minus);
	else{
		for (i=0 ; i<num_digit+num_minus-num_column ; i++)
			no /= 10;
		num_digit -= num_digit+num_minus-num_column;
	}

	if (num_minus == 1) no = -no;
	char format[6];
	if (is_zero_inserted)
		sprintf(format, "%%0%dd", num_column);
	else
	sprintf(format, "%%%dd", num_column);
	sprintf(buffer, format, no);

	return num_column;
}

int ScriptHandler::readScriptSub( FILE *fp, char **buf, int encrypt_mode )
{
	char magic[5] = {0x79, 0x57, 0x0d, 0x80, 0x04 };
	int  magic_counter = 0;
	bool newline_flag = true;
	bool cr_flag = false;

	if (encrypt_mode == 3 && !key_table_flag)
		errorAndExit("readScriptSub: the EXE file must be specified with --key-exe option.");

	size_t len=0, count=0;
	while(1){
		if (len == count){
			len = fread(tmp_script_buf, 1, TMP_SCRIPT_BUF_LEN, fp);
			if (len == 0){
				if (cr_flag) *(*buf)++ = 0x0a;
				break;
			}
			count = 0;
		}
		char ch = tmp_script_buf[count++];
		if      ( encrypt_mode == 1 ) ch ^= 0x84;
		else if ( encrypt_mode == 2 ){
			ch = (ch ^ magic[magic_counter++]) & 0xff;
			if ( magic_counter == 5 ) magic_counter = 0;
		}
		else if ( encrypt_mode == 3){
			ch = key_table[(unsigned char)ch] ^ 0x84;
		}

		if ( cr_flag && ch != 0x0a ){
			*(*buf)++ = 0x0a;
			newline_flag = true;
			cr_flag = false;
		}

		if ( ch == '*' && newline_flag ) num_of_labels++;
		if ( ch == 0x0d ){
			cr_flag = true;
			continue;
		}
		if ( ch == 0x0a ){
			*(*buf)++ = 0x0a;
			newline_flag = true;
			cr_flag = false;
		}
		else{
			*(*buf)++ = ch;
			if ( ch != ' ' && ch != '\t' )
				newline_flag = false;
		}
	}

	*(*buf)++ = 0x0a;
	return 0;
}

int ScriptHandler::readScript( char *path )
{
	archive_path = new char[ strlen(path) + 1 ];
	strcpy( archive_path, path );

	FILE *fp = NULL;
	char filename[10];
	int i, encrypt_mode = 0;
	if ((fp = fopen("0.txt", "rb")) != NULL){
		encrypt_mode = 0;
	}
	else if ((fp = fopen("00.txt", "rb")) != NULL){
		encrypt_mode = 0;
	}
	else if ((fp = fopen("nscr_sec.dat", "rb")) != NULL){
		encrypt_mode = 2;
	}
	else if ((fp = fopen("nscript.___", "rb")) != NULL){
		encrypt_mode = 3;
	}
	else if ((fp = fopen("nscript.dat", "rb")) != NULL){
		encrypt_mode = 1;
	}

	if (fp == NULL){
		fprintf( stderr, "can't open any of 0.txt, 00.txt, nscript.dat and nscript.___\n" );
		return -1;
	}

	fseek( fp, 0, SEEK_END );
	int estimated_buffer_length = ftell( fp ) + 1;

	if (encrypt_mode == 0){
		fclose(fp);
		for (i=1 ; i<100 ; i++){
			sprintf(filename, "%d.txt", i);
			if ((fp = fopen(filename, "rb")) == NULL){
				sprintf(filename, "%02d.txt", i);
				fp = fopen(filename, "rb");
			}
			if (fp){
				fseek( fp, 0, SEEK_END );
				estimated_buffer_length += ftell(fp)+1;
				fclose(fp);
			}
		}
	}

	if ( script_buffer ) delete[] script_buffer;
	script_buffer = new char[ estimated_buffer_length ];

	char *p_script_buffer;
	current_script = p_script_buffer = script_buffer;

	tmp_script_buf = new char[TMP_SCRIPT_BUF_LEN];
	if (encrypt_mode > 0){
		fseek( fp, 0, SEEK_SET );
		readScriptSub( fp, &p_script_buffer, encrypt_mode );
		fclose( fp );
	}
	else{
		for (i=0 ; i<100 ; i++){
			sprintf(filename, "%d.txt", i);
			if ((fp = fopen(filename, "rb")) == NULL){
				sprintf(filename, "%02d.txt", i);
				fp = fopen(filename, "rb");
			}
			if (fp){
				readScriptSub( fp, &p_script_buffer, 0 );
				fclose(fp);
			}
		}
	}
	delete[] tmp_script_buf;

	script_buffer_length = p_script_buffer - script_buffer;

	/* ---------------------------------------- */
	/* screen size and value check */
	char *buf = script_buffer+1;
	while( script_buffer[0] == ';' ){
		if ( !strncmp( buf, "mode", 4 ) ){
			buf += 4;
			if      ( !strncmp( buf, "800", 3 ) )
				screen_size = SCREEN_SIZE_800x600;
			else if ( !strncmp( buf, "400", 3 ) )
				screen_size = SCREEN_SIZE_400x300;
			else if ( !strncmp( buf, "320", 3 ) )
				screen_size = SCREEN_SIZE_320x240;
			else
				screen_size = SCREEN_SIZE_640x480;
			buf += 3;
		}
		else if ( !strncmp( buf, "value", 5 ) ){
			buf += 5;
			global_variable_border = 0;
			while ( *buf >= '0' && *buf <= '9' )
				global_variable_border = global_variable_border * 10 + *buf++ - '0';
		}
		else{
			break;
		}
		if ( *buf != ',' ){
			while ( *buf++ != '\n' );
			break;
		}
		buf++;
	}
	// game ID check
	if ( *buf++ == ';' ){
		while (*buf == ' ' || *buf == '\t') ++buf;
		if ( !strncmp( buf, "gameid ", 7 ) ){
			buf += 7;
			int i = 0;
			while ( buf[i++] >= ' ' );
			game_identifier = new char[i];
			strncpy( game_identifier, buf, i - 1 );
			game_identifier[i - 1] = 0;
			buf += i;
		}
	}
	// font check
	if ( *buf++ == ';' ){
		while (*buf == ' ' || *buf == '\t') ++buf;
		if ( !strncmp( buf, "usefont ", 8 ) ){
			buf += 8;
			int i = 0;
			while ( buf[i++] >= ' ' );
			script_defined_font = new char[i];
			strncpy( script_defined_font, buf, i - 1 );
			script_defined_font[i - 1] = 0;
		}
	}

	return labelScript();
}

int ScriptHandler::labelScript()
{
	int label_counter = -1;
	int current_line = 0;
	char *buf = script_buffer;
	label_info = new LabelInfo[ num_of_labels+1 ];

	while ( buf < script_buffer + script_buffer_length ){
		SKIP_SPACE( buf );
		if ( *buf == '*' ){
			setCurrent( buf );
			readLabel();
			label_info[ ++label_counter ].name = new char[ strlen(string_buffer) ];
			strcpy( label_info[ label_counter ].name, string_buffer+1 );
			label_info[ label_counter ].label_header = buf;
			label_info[ label_counter ].num_of_lines = 1;
			label_info[ label_counter ].start_line   = current_line;
			buf = getNext();
			if ( *buf == 0x0a ){
				buf++;
				SKIP_SPACE(buf);
				current_line++;
			}
			label_info[ label_counter ].start_address = buf;
		}
		else{
			if ( label_counter >= 0 )
				label_info[ label_counter ].num_of_lines++;
			while( *buf != 0x0a ) buf++;
			buf++;
			current_line++;
		}
	}

	label_info[num_of_labels].start_address = NULL;

	return 0;
}

struct ScriptHandler::LabelInfo ScriptHandler::lookupLabel( const char *label )
{
	int i = findLabel( label );

	findAndAddLog( log_info[LABEL_LOG], label_info[i].name, true );
	return label_info[i];
}

struct ScriptHandler::LabelInfo ScriptHandler::lookupLabelNext( const char *label )
{
	int i = findLabel( label );
	if ( i+1 < num_of_labels ){
		findAndAddLog( log_info[LABEL_LOG], label_info[i+1].name, true );
		return label_info[i+1];
	}

	return label_info[num_of_labels];
}

ScriptHandler::LogLink *ScriptHandler::findAndAddLog( LogInfo &info, const char *name, bool add_flag )
{
	char capital_name[256];
	for ( unsigned int i=0 ; i<strlen(name)+1 ; i++ ){
		capital_name[i] = name[i];
		if ( 'a' <= capital_name[i] && capital_name[i] <= 'z' ) capital_name[i] += 'A' - 'a';
		else if ( capital_name[i] == '/' ) capital_name[i] = '\\';
	}

	LogLink *cur = info.root_log.next;
	while( cur ){
		if ( !strcmp( cur->name, capital_name ) ) break;
		cur = cur->next;
	}
	if ( !add_flag || cur ) return cur;

	LogLink *link = new LogLink();
	link->name = new char[strlen(capital_name)+1];
	strcpy( link->name, capital_name );
	info.current_log->next = link;
	info.current_log = info.current_log->next;
	info.num_logs++;

	return link;
}

void ScriptHandler::resetLog( LogInfo &info )
{
	LogLink *link = info.root_log.next;
	while( link ){
		LogLink *tmp = link;
		link = link->next;
		delete tmp;
	}

	info.root_log.next = NULL;
	info.current_log = &info.root_log;
	info.num_logs = 0;
}

ScriptHandler::ArrayVariable *ScriptHandler::getRootArrayVariable(){
	return root_array_variable;
}

void ScriptHandler::addNumAlias( const char *str, int no )
{
	Alias *p_num_alias = new Alias( str, no );
	last_num_alias->next = p_num_alias;
	last_num_alias = last_num_alias->next;
}

void ScriptHandler::addStrAlias( const char *str1, const char *str2 )
{
	Alias *p_str_alias = new Alias( str1, str2 );
	last_str_alias->next = p_str_alias;
	last_str_alias = last_str_alias->next;
}

void ScriptHandler::errorAndExit( char *str )
{
	fprintf( stderr, " **** Script error, %s [%s] ***\n", str, string_buffer );
	exit(-1);
}

void ScriptHandler::addStringBuffer( char ch )
{
	if (string_counter+1 == STRING_BUFFER_LENGTH)
		errorAndExit("addStringBuffer: string exceeds 2048.");
	string_buffer[string_counter++] = ch;
	string_buffer[string_counter] = '\0';
}

// ----------------------------------------
// Private methods

int ScriptHandler::findLabel( const char *label )
{
	int i;
	char capital_label[256];

	for ( i=0 ; i<(int)strlen( label )+1 ; i++ ){
		capital_label[i] = label[i];
		if ( 'A' <= capital_label[i] && capital_label[i] <= 'Z' ) capital_label[i] += 'a' - 'A';
	}
	for ( i=0 ; i<num_of_labels ; i++ ){
		if ( !strcmp( label_info[i].name, capital_label ) )
			return i;
	}

	char *p = new char[ strlen(label) + 32 ];
	sprintf(p, "Label \"%s\" is not found.", label);
	errorAndExit( p );

	return -1; // dummy
}

char *ScriptHandler::checkComma( char *buf )
{
	SKIP_SPACE( buf );
	if (*buf == ','){
		end_status |= END_COMMA;
		buf++;
		SKIP_SPACE( buf );
	}

	return buf;
}

void ScriptHandler::parseStr( char **buf )
{
	SKIP_SPACE( *buf );

	if ( **buf == '(' ){
		(*buf)++;
		parseStr(buf);
		SKIP_SPACE( *buf );
		if ( (*buf)[0] != ')' ) errorAndExit("parseStr: ) is not found.");
		(*buf)++;

		if ( findAndAddLog( log_info[FILE_LOG], str_string_buffer, false ) ){
			parseStr(buf);
			char *tmp_buf = new char[ strlen( str_string_buffer ) + 1 ];
			strcpy( tmp_buf, str_string_buffer );
			parseStr(buf);
			strcpy( str_string_buffer, tmp_buf );
			delete[] tmp_buf;
		}
		else{
			parseStr(buf);
			parseStr(buf);
		}
		current_variable.type |= VAR_CONST;
	}
	else if ( **buf == '$' ){
		(*buf)++;
		int no = parseInt(buf);
		if ( variable_data[no].str )
			strcpy( str_string_buffer, variable_data[no].str );
		else
			str_string_buffer[0] = '\0';
		current_variable.type = VAR_STR;
		current_variable.var_no = no;
		if (current_variable.var_no < 0 ||
			current_variable.var_no >= VARIABLE_RANGE)
			current_variable.var_no = VARIABLE_RANGE;
	}
	else if ( **buf == '"' ){
		int c=0;
		(*buf)++;
		while ( **buf != '"' && **buf != 0x0a )
			str_string_buffer[c++] = *(*buf)++;
		str_string_buffer[c] = '\0';
		if ( **buf == '"' ) (*buf)++;
		current_variable.type |= VAR_CONST;
	}
	else if ( **buf == '`' ){
		int c=0;
		str_string_buffer[c++] = *(*buf)++;

//		while ( **buf != '`' && **buf != 0x0a )
//			str_string_buffer[c++] = *(*buf)++;

		char encoding = default_encoding;
		char ch = **buf;
		while (ch != '`' && ch != 0x0a && ch !='\0'){
			if (ch == '~' && (ch = *++(*buf)) != '~') {
				encoding = ch;
				ch = *++(*buf);
				if (ch == '~') ch = *++(*buf);
				continue;
			}
			if (encoding != 'r') {
				const unsigned short uc = UnicodeOfUTF8(*buf);
				*buf += CharacterBytes(&ch);
				char b2[5];
				UTF8OfUnicode(get_encoded_char(encoding, uc), b2);
				for (int i = 0; b2[i]; ++i) str_string_buffer[c++] = b2[i];
			}
			else {
				char bytes = CharacterBytes(&ch);
				while (bytes--) str_string_buffer[c++] = *(*buf)++;
			}
			ch = **buf;
		}

		str_string_buffer[c] = '\0';
		if ( **buf == '`' ) (*buf)++;
		current_variable.type |= VAR_CONST;
		end_status |= END_1BYTE_CHAR;
	}
	else if ( **buf == '#' ){ // for color
		for ( int i=0 ; i<7 ; i++ )
			str_string_buffer[i] = *(*buf)++;
		str_string_buffer[7] = '\0';
		current_variable.type = VAR_NONE;
	}
	else{ // str alias
		char ch, alias_buf[512];
		int alias_buf_len = 0;
		bool first_flag = true;

		while(1){
			if ( alias_buf_len == 511 ) break;
			ch = **buf;

			if ( (ch >= 'a' && ch <= 'z') ||
				 (ch >= 'A' && ch <= 'Z') ||
				 ch == '_' ){
				if (ch >= 'A' && ch <= 'Z') ch += 'a' - 'A';
				first_flag = false;
				alias_buf[ alias_buf_len++ ] = ch;
			}
			else if ( ch >= '0' && ch <= '9' ){
				if ( first_flag ) errorAndExit("parseStr: number is not allowed for the first letter of str alias.");
				first_flag = false;
				alias_buf[ alias_buf_len++ ] = ch;
			}
			else break;
			(*buf)++;
		}
		alias_buf[alias_buf_len] = '\0';

		if ( alias_buf_len == 0 ){
			str_string_buffer[0] = '\0';
			current_variable.type = VAR_NONE;
			return;
		}

		Alias *p_str_alias = root_str_alias.next;

		while( p_str_alias ){
			if ( !strcmp( p_str_alias->alias, (const char*)alias_buf ) ){
				strcpy( str_string_buffer, p_str_alias->str );
				break;
			}
			p_str_alias = p_str_alias->next;
		}
		if ( !p_str_alias ){
			printf("can't find str alias for %s...\n", alias_buf );
			exit(-1);
		}
		current_variable.type |= VAR_CONST;
	}
}

int ScriptHandler::parseInt( char **buf )
{
	int ret = 0;

	SKIP_SPACE( *buf );

	if ( **buf == '%' ){
		(*buf)++;
		current_variable.var_no = parseInt(buf);
		if (current_variable.var_no < 0 ||
			current_variable.var_no >= VARIABLE_RANGE)
			current_variable.var_no = VARIABLE_RANGE;
		current_variable.type = VAR_INT;
		return variable_data[ current_variable.var_no ].num;
	}
	else if ( **buf == '?' ){
		ArrayVariable av;
		current_variable.var_no = parseArray( buf, av );
		current_variable.type = VAR_ARRAY;
		current_variable.array = av;
		return *getArrayPtr( current_variable.var_no, current_variable.array, 0 );
	}
	else{
		char ch, alias_buf[256];
		int alias_buf_len = 0, alias_no = 0;
		bool direct_num_flag = false;
		bool num_alias_flag = false;

		char *buf_start = *buf;
		while( 1 ){
			ch = **buf;

			if ( (ch >= 'a' && ch <= 'z') ||
				 (ch >= 'A' && ch <= 'Z') ||
				 ch == '_' ){
				if (ch >= 'A' && ch <= 'Z') ch += 'a' - 'A';
				if ( direct_num_flag ) break;
				num_alias_flag = true;
				alias_buf[ alias_buf_len++ ] = ch;
			}
			else if ( ch >= '0' && ch <= '9' ){
				if ( !num_alias_flag ) direct_num_flag = true;
				if ( direct_num_flag )
					alias_no = alias_no * 10 + ch - '0';
				else
					alias_buf[ alias_buf_len++ ] = ch;
			}
			else break;
			(*buf)++;
		}

		if ( *buf - buf_start  == 0 ){
			current_variable.type = VAR_NONE;
			return 0;
		}

		/* ---------------------------------------- */
		/* Solve num aliases */
		if ( num_alias_flag ){
			alias_buf[ alias_buf_len ] = '\0';
			Alias *p_num_alias = root_num_alias.next;

			while( p_num_alias ){
				if ( !strcmp( p_num_alias->alias,
							  (const char*)alias_buf ) ){
					alias_no = p_num_alias->num;
					break;
				}
				p_num_alias = p_num_alias->next;
			}
			if ( !p_num_alias ){
				//printf("can't find num alias for %s... assume 0.\n", alias_buf );
				current_variable.type = VAR_NONE;
				*buf = buf_start;
				return 0;
			}
		}
		current_variable.type = VAR_INT | VAR_CONST;
		ret = alias_no;
	}

	SKIP_SPACE( *buf );

	return ret;
}

int ScriptHandler::parseIntExpression( char **buf )
{
	int num[3], op[2]; // internal buffer

	SKIP_SPACE( *buf );

	readNextOp( buf, NULL, &num[0] );

	readNextOp( buf, &op[0], &num[1] );
	if ( op[0] == OP_INVALID )
		return num[0];

	while(1){
		readNextOp( buf, &op[1], &num[2] );
		if ( op[1] == OP_INVALID ) break;

		if ( !(op[0] & 0x04) && (op[1] & 0x04) ){ // if priority of op[1] is higher than op[0]
			num[1] = calcArithmetic( num[1], op[1], num[2] );
		}
		else{
			num[0] = calcArithmetic( num[0], op[0], num[1] );
			op[0] = op[1];
			num[1] = num[2];
		}
	}
	return calcArithmetic( num[0], op[0], num[1] );
}

/*
 * Internal buffer looks like this.
 *   num[0] op[0] num[1] op[1] num[2]
 * If priority of op[0] is higher than op[1], (num[0] op[0] num[1]) is computed,
 * otherwise (num[1] op[1] num[2]) is computed.
 * Then, the next op and num is read from the script.
 * Num is an immediate value, a variable or a bracketed expression.
 */
void ScriptHandler::readNextOp( char **buf, int *op, int *num )
{
	bool minus_flag = false;
	SKIP_SPACE(*buf);
	char *buf_start = *buf;

	if ( op ){
		if      ( (*buf)[0] == '+' ) *op = OP_PLUS;
		else if ( (*buf)[0] == '-' ) *op = OP_MINUS;
		else if ( (*buf)[0] == '*' ) *op = OP_MULT;
		else if ( (*buf)[0] == '/' ) *op = OP_DIV;
		else if ( (*buf)[0] == 'm' &&
				  (*buf)[1] == 'o' &&
				  (*buf)[2] == 'd' &&
				  ( (*buf)[3] == ' '  ||
					(*buf)[3] == '\t' ||
					(*buf)[3] == '$' ||
					(*buf)[3] == '%' ||
					(*buf)[3] == '?' ||
					( (*buf)[3] >= '0' && (*buf)[3] <= '9') ))
			*op = OP_MOD;
		else{
			*op = OP_INVALID;
			return;
		}
		if ( *op == OP_MOD ) *buf += 3;
		else                 (*buf)++;
		SKIP_SPACE(*buf);
	}
	else{
		if ( (*buf)[0] == '-' ){
			minus_flag = true;
			(*buf)++;
			SKIP_SPACE(*buf);
		}
	}

	if ( (*buf)[0] == '(' ){
		(*buf)++;
		*num = parseIntExpression( buf );
		if (minus_flag) *num = -*num;
		SKIP_SPACE(*buf);
		if ( (*buf)[0] != ')' ) errorAndExit(") is not found.");
		(*buf)++;
	}
	else{
		*num = parseInt( buf );
		if (minus_flag) *num = -*num;
		if ( current_variable.type == VAR_NONE ){
			if (op) *op = OP_INVALID;
			*buf = buf_start;
		}
	}
}

int ScriptHandler::calcArithmetic( int num1, int op, int num2 )
{
	int ret=0;

	if      ( op == OP_PLUS )  ret = num1+num2;
	else if ( op == OP_MINUS ) ret = num1-num2;
	else if ( op == OP_MULT )  ret = num1*num2;
	else if ( op == OP_DIV )   ret = num1/num2;
	else if ( op == OP_MOD )   ret = num1%num2;

	current_variable.type = VAR_INT | VAR_CONST;

	return ret;
}

int ScriptHandler::parseArray( char **buf, struct ArrayVariable &array )
{
	SKIP_SPACE( *buf );

	(*buf)++; // skip '?'
	int no = parseInt( buf );

	SKIP_SPACE( *buf );
	array.num_dim = 0;
	while ( **buf == '[' ){
		(*buf)++;
		array.dim[array.num_dim] = parseIntExpression(buf);
		array.num_dim++;
		SKIP_SPACE( *buf );
		if ( **buf != ']' ) errorAndExit( "parseArray: no ']' is found." );
		(*buf)++;
	}
	for ( int i=array.num_dim ; i<20 ; i++ ) array.dim[i] = 0;

	return no;
}

int *ScriptHandler::getArrayPtr( int no, ArrayVariable &array, int offset )
{
	ArrayVariable *av = root_array_variable;
	while(av){
		if (av->no == no) break;
		av = av->next;
	}
	if (av == NULL) errorAndExit( "Array No. is not declared." );

	int dim = 0, i;
	for ( i=0 ; i<av->num_dim ; i++ ){
		if ( av->dim[i] <= array.dim[i] ) errorAndExit( "dim[i] <= array.dim[i]." );
		dim = dim * av->dim[i] + array.dim[i];
	}
	if ( av->dim[i-1] <= array.dim[i-1] + offset ) errorAndExit( "dim[i-1] <= array.dim[i-1] + offset." );

	return &av->data[dim+offset];
}

void ScriptHandler::declareDim()
{
	current_script = next_script;
	char *buf = current_script;

	if (current_array_variable){
		current_array_variable->next = new ArrayVariable();
		current_array_variable = current_array_variable->next;
	}
	else{
		root_array_variable = new ArrayVariable();
		current_array_variable = root_array_variable;
	}

	ArrayVariable array;
	current_array_variable->no = parseArray( &buf, array );

	int dim = 1;
	current_array_variable->num_dim = array.num_dim;
	for ( int i=0 ; i<array.num_dim ; i++ ){
		current_array_variable->dim[i] = array.dim[i]+1;
		dim *= (array.dim[i]+1);
	}
	current_array_variable->data = new int[dim];
	memset( current_array_variable->data, 0, sizeof(int) * dim );

	next_script = buf;
}