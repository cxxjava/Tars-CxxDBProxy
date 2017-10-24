#ifndef _DBProxyServer_H_
#define _DBProxyServer_H_

#include <iostream>
#include "servant/Application.h"

#include "Efc.hh"
#include "./dblib/inc/EDatabase.hh"

using namespace efc::edb;

/**
 *
 *
 */

#define PROXY_VERSION "version 0.1.0"

#define DEFAULT_CONNECT_PORT 6633
#define DEFAULT_CONNECT_SSL_PORT 6643

extern "C" {
typedef efc::edb::EDatabase* (make_database_t)(EDBProxyInf* proxy);
}

struct VirtualDB: public EObject {
	EString alias_dbname;

	int connectTimeout;
	int socketTimeout;
	EString dbType;
	EString clientEncoding;
	EString database;
	EString host;
	int port;
	EString username;
	EString password;

	VirtualDB(EString& alias, int ct, int st, EString& dt, EString& ce, EString& db, EString& h, int port, EString& usr, EString& pwd) :
		alias_dbname(alias),
		connectTimeout(ct),
		socketTimeout(st),
		dbType(dt),
		clientEncoding(ce),
		database(db),
		host(h),
		port(port),
		username(usr),
		password(pwd) {
	}
};

struct QueryUser: public EObject {
	EString username;
	EString password;
	VirtualDB *virdb;

	QueryUser(const char* username, const char* password, VirtualDB* vdb):
		username(username), password(password), virdb(vdb) {
	}
};

class VirtualDBManager: public EObject {
public:
	VirtualDBManager(EConfig& conf);
	EHashMap<EString*,VirtualDB*>& getDBMap();
	VirtualDB* getVirtualDB(EString* alias_dbname);

private:
	EConfig& conf;
	EHashMap<EString*,VirtualDB*> virdbMap;

	VirtualDB* parseUrl(EString alias_dbname, const char* url);
};

class VirtualUserManager: public EObject {
public:
	VirtualUserManager(EConfig& conf, VirtualDBManager& dbs);
	EHashMap<EString*,QueryUser*>& getUserMap();
	QueryUser* checkUser(const char* username, const char* password, llong timestamp);

private:
	EConfig& conf;
	VirtualDBManager& dbs;
	EHashMap<EString*,QueryUser*> userMap;
};

class DBSoHandleManager: public EObject {
public:
	~DBSoHandleManager();
	DBSoHandleManager(EConfig& conf);
	void soOpenAll();
	void soCloseAll();
	void testConnectAll();
	sp<EDatabase> getDatabase(const char* dbtype, efc::edb::EDBProxyInf* proxy);

private:
	struct SoHandle: public EObject {
		es_dso_t *handle;
		make_database_t *func;
	};
	EConfig& conf;
	EHashMap<EString*,SoHandle*> handles;
};

//=============================================================================


using namespace tars;

/**
 *
 **/

class DBProxyServerImp;

class DBProxyServer : public Application
{
public:
    /**
     *
     **/
    virtual ~DBProxyServer() {};

    /**
     *
     **/
    virtual void initialize();

    /**
     *
     **/
    virtual void destroyApp();

private:
    friend class DBProxyServantImp;

    EConfig conf;
	DBSoHandleManager* somgr;
	VirtualDBManager* dbmgr;
	VirtualUserManager* usrmgr;
	EHashMap<int, sp<EDatabase> > dbmap;
};

extern DBProxyServer g_app;

////////////////////////////////////////////
#endif
