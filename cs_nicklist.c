#include "module.h"
#include <mysql.h>

#define AUTHOR "Frederick Parotat"
#define VERSION "1.0"

#define GET_MYSQL_ERROR      mysql_error(mysql_obj)

// MySQL_Config:
char *MSQL_HOST = NULL;
unsigned int MSQL_PORT = 0;
char *MSQL_USER = NULL;
char *MSQL_PASS = NULL;
char *MSQL_DB = NULL;

/* MySQL Variables */
MYSQL *mysql_obj = NULL;
MYSQL_RES *mysql_result_set = NULL;
my_ulonglong q_mysql_affected_rows;

/* Main module methods */
int load_config();
int reload_config(int ac, char **av);
int do_change(int ac, char **av);

/* Channel Methods */
bool channel_exists(char *chan);
bool channel_watch(char *chan);
int channel_add(char *chan);
int channel_update(char *chan);

/* Helper methods */
int do_mysql_connect();
unsigned int do_mysql_query(char *format, ...);
void do_mysql_close();

/**
 * Called when module is loaded.
 **/
int AnopeInit(void)
{
	EvtHook *hook;

	moduleAddAuthor(AUTHOR);
	moduleAddVersion(VERSION);

	hook = createEventHook(EVENT_JOIN_CHANNEL, do_change);
	if (moduleAddEventHook(hook) != MOD_ERR_OK)
		return MOD_STOP;

	hook = createEventHook(EVENT_PART_CHANNEL, do_change);
	if (moduleAddEventHook(hook) != MOD_ERR_OK)
		return MOD_STOP;

	hook = createEventHook(EVENT_RELOAD, reload_config);
	if (moduleAddEventHook(hook) != MOD_ERR_OK)
		return MOD_STOP;

	if (load_config() == MOD_STOP) return MOD_STOP;

	if (do_mysql_connect())
	{
		alog("[\002cs_nicklist\002] Can't connect to MySQL server");
		return MOD_STOP;
	}

	alog("\002cs_nicklist\002 geladen... [Author: \002%s\002] [Version: \002%s\002]", AUTHOR, VERSION);

	return MOD_CONT;
}

/**
 * Called when module is unloaded
 **/
void AnopeFini(void)
{
	if (MSQL_HOST) { free(MSQL_HOST); MSQL_HOST = NULL; }
	if (MSQL_USER) { free(MSQL_USER); MSQL_USER = NULL; }
	if (MSQL_PASS) { free(MSQL_PASS); MSQL_PASS = NULL; }
	if (MSQL_DB)   { free(MSQL_DB);   MSQL_DB = NULL;   }

	do_mysql_close();

	alog("[\002cs_nicklist\002]: Modul entladen");
}

/* Main module methods */
int load_config(void)
{
	Directive confvalues[][1] = {
		{{ "NicklistMysqlHost",	{ { PARAM_STRING, PARAM_RELOAD, &MSQL_HOST	} } }},
		{{ "NicklistMysqlPort",	{ { PARAM_PORT,	  PARAM_RELOAD, &MSQL_PORT	} } }},
		{{ "NicklistMysqlUser",	{ { PARAM_STRING, PARAM_RELOAD, &MSQL_USER	} } }},
		{{ "NicklistMysqlPass",	{ { PARAM_STRING, PARAM_RELOAD, &MSQL_PASS	} } }},
		{{ "NicklistMysqlName",	{ { PARAM_STRING, PARAM_RELOAD, &MSQL_DB	} } }}
	};

	MSQL_PORT = 3306;
	if(MSQL_HOST) { free(MSQL_HOST); MSQL_HOST = NULL; }
	if(MSQL_USER) { free(MSQL_USER); MSQL_USER = NULL; }
	if(MSQL_PASS) { free(MSQL_PASS); MSQL_PASS = NULL; }
	if(MSQL_DB)   { free(MSQL_DB);   MSQL_DB = NULL;   }

	unsigned int i = 0;
	for (i = 0; i < 7; i++)
	{
		moduleGetConfigDirective(confvalues[i]);
	}

	if(!MSQL_USER || !MSQL_PASS || !MSQL_DB || !MSQL_HOST)
	{
		alog("[\002cs_nicklist\002]: Fehlende Konfigutarion!");
		return MOD_STOP;
	}

	return MOD_CONT;
}

int reload_config(int ac, char **av)
{
	if(ac >= 1)
	{
		if(!stricmp(av[0], EVENT_START))
		{
			alog("[\002cs_nicklist\002]: Neuladen der Konfiguration...");
			return load_config;
		}
	}
	return MOD_CONT;
}

int do_change(int ac, char **av)
{
	ChannelInfo *ci;
	User *u;
	Channel *c;

	if (!(ci = cs_findchan(av[2]))) return MOD_CONT;
	if (!(c = findchan(ci->name)))  return MOD_CONT;

	if(channel_exists(av[2]))
	{
		if(channel_watch(av[2]))
		{
			channel_update(av[2]);
		}
	}
	else
		channel_add(av[2]);

	return MOD_CONT;
}

/* Channel Methods */
bool channel_exists(char *chan)
{
	unsigned int ret;

	ret = do_mysql_query("SELECT channel FROM `%s`.`cs_nl_channels` WHERE channel = %s;", MSQL_DB, chan);
	
	MYSQL_ROW row = mysql_fetch_row(mysql_result_set);

	if(mysql_num_rows(mysql_result_set) == 0)
		return false;
	else
		return true;
}

bool channel_watch(char *chan)
{
	unsigned int ret;

	if(channel_exists(chan))
	{
		ret = do_mysql_query("SELECT watch FROM `%s`.`cs_nl_channels` WHERE channel = %s;", MSQL_DB, chan);

		MYSQL_ROW row = mysql_fetch_row(mysql_result_set);

		if(row)
		{
			unsigned long *lengths = mysql_fetch_lengths(mysql_result_set);
			int watch = (int)row[0];
			if(watch == 1)
				return true;
			else
				return false;
		}

	}
	else
		return false;
}

int channel_add(char *chan)
{
	do_mysql_query("INSERT INTO `%s`.`cs_nl_channels` (channel, watch) VALUES('%s', '0' )", MSQL_DB, chan);
	return 1;
}

int channel_update(char *chan)
{
	Channel *c;
	ChannelInfo *ci;

	if((c = findchan(chan)) && (ci = c->ci))
	{
		do_mysql_query("DELETE FROM `%s`.`cs_nl_users` WHERE channel='%s'", MSQL_DB, chan);

		struct c_userlist *cu, *next;

		cu = c->users;
		while (cu) {
			next = cu->next;

			do_mysql_query("INSERT INTO `%s`.`cs_nl_users` (channel, nick) VALUES('%s', '%s' )", MSQL_DB, chan, cu->user);
			
			cu = next;
		}
	}
}

/* Helper methods */
int do_mysql_connect()
{
	if(mysql_obj)
		return 0;

	mysql_obj = mysql_init(NULL);

	if(!mysql_real_connect(mysql_obj, MSQL_HOST, MSQL_USER, MSQL_PASS, MSQL_DB, MSQL_PORT, NULL, CLIENT_IGNORE_SPACE))
	{
		mysql_close(mysql_obj);
		mysql_obj = NULL;
		return 1;
	}
	else
	{
#ifdef MYSQL_OPT_RECONNECT
		my_bool my_true = true;
		mysql_options(mysql_obj, MYSQL_OPT_RECONNECT, &my_true);
#else
		mysql_obj->reconnect = 1;
#endif
		return 0;
	}
}

unsigned int do_mysql_query(char *format, ...)
{
	va_list args;
	va_start(args,format);
	char *query = vstr(format, args);
	va_end(args);

	if (mysql_result_set)
	{
		mysql_free_result(mysql_result_set);
		mysql_result_set = NULL;
	}

	if (mysql_query(mysql_obj, query))
	{
		free(query);
		return 1;
	}
  
	free(query);

	mysql_result_set = mysql_store_result(mysql_obj);
	q_mysql_affected_rows = mysql_affected_rows(mysql_obj);

	return 0;
}

void do_mysql_close()
{
	if (mysql_result_set)
	{
		mysql_free_result(mysql_result_set);
		mysql_result_set = NULL;
	}
	mysql_close(mysql_obj);
	mysql_obj = NULL;
}