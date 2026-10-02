/*
*
* m1_at_response_parser.h
*
* M1 parser for EPS32 module
*
* B.N @ MonstaTek, Jul 2025
*
*/

#ifndef M1_AT_RESPONSE_PARSER_H_
#define M1_AT_RESPONSE_PARSER_H_

char *m1_resp_string_strip(char *resp, const char *substr);
uint8_t m1_parse_spi_at_resp(char *resp, const char *resp_key, ctrl_cmd_t *app_resp);

#endif /* M1_AT_RESPONSE_PARSER_H_ */
