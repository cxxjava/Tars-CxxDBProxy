#include "DBProxyServer.h"
#include "DBProxyServantImp.h"
#include "./interface/EDBInterface.h"
#include "servant/TarsLogger.h"

using namespace std;

DBProxyServer g_app;

/////////////////////////////////////////////////////////////////

VirtualDBManager::VirtualDBManager(EConfig& conf): conf(conf) {
	EConfig* subc = conf.getConfig("DBLIST");
	if (!subc) {
		throw EIllegalArgumentException(__FILE__, __LINE__, "[DBLIST] is null.");
	}
	EArray<EString*> usrs = subc->keyNames();
	for (int i=0; i<usrs.size(); i++) {
		const char* alias_dbname = usrs[i]->c_str();
		EString url = subc->getString(alias_dbname);
		VirtualDB* vdb = parseUrl(alias_dbname, url.c_str());
		if (vdb) {
			virdbMap.put(new EString(alias_dbname), vdb);
		}
	}
}

EHashMap<EString*,VirtualDB*>& VirtualDBManager::getDBMap() {
	return virdbMap;
}

VirtualDB* VirtualDBManager::getVirtualDB(EString* alias_dbname) {
	return virdbMap.get(alias_dbname);
}

VirtualDB* VirtualDBManager::parseUrl(EString alias_dbname, const char* url) {
	if (!url || !*url) {
		throw ENullPointerException(__FILE__, __LINE__);
	}

	if (eso_strncmp(url, "edbc:", 5) != 0) {
		throw EProtocolException(__FILE__, __LINE__, "edbc:");
	}

	//edbc:[DBTYPE]://host:port/database?connectTimeout=xxx&socketTimeout=xxx&clientEncoding=xxx&username=xxx&password=xxx

	EURI uri(url + 5 /*edbc:*/);
	EString dbType = uri.getScheme();
	EString host = uri.getHost();
	int port = uri.getPort();
	EString database = uri.getPath();
	database = database.substring(1); //index 0 is '/'
	int connectTimeout = 30;
	int socketTimeout = 30;
	EString clientEncoding("utf8");
	EString ct = uri.getParameter("connectTimeout");
	if (!ct.isEmpty()) {
		connectTimeout = EInteger::parseInt(ct.c_str());
	}
	EString st = uri.getParameter("socketTimeout");
	if (!st.isEmpty()) {
		socketTimeout = EInteger::parseInt(st.c_str());
	}
	EString ce = uri.getParameter("clientEncoding");
	if (!ce.isEmpty()) {
		clientEncoding = ce;
	}

	if (database.isEmpty() || host.isEmpty() || database.isEmpty()) {
		throw EIllegalArgumentException(__FILE__, __LINE__, "url");
	}
	if (port == -1) {
		port = DEFAULT_CONNECT_PORT;
	}
	EString username = uri.getParameter("username");
	EString password = uri.getParameter("password");

	return new VirtualDB(alias_dbname, connectTimeout, socketTimeout, dbType,
			clientEncoding, database, host, port, username, password);
}

//=============================================================================

VirtualUserManager::VirtualUserManager(EConfig& conf, VirtualDBManager& dbs): conf(conf), dbs(dbs) {
	EConfig* subc = conf.getConfig("USERMAP");
	if (!subc) {
		throw EIllegalArgumentException(__FILE__, __LINE__, "[USERMAP] is null.");
	}
	EArray<EString*> usrs = subc->keyNames();
	for (int i=0; i<usrs.size(); i++) {
		const char* username = usrs[i]->c_str();
		EString value = subc->getString(username);
		EArray<EString*> ss = EPattern::split(",", value.c_str());
		if (ss.size() >= 2) {
			userMap.put(new EString(username), new QueryUser(username, ss[1]->c_str(), dbs.getVirtualDB(ss[0])));
		}
	}
}
EHashMap<EString*,QueryUser*>& VirtualUserManager::getUserMap() {
	return userMap;
}
QueryUser* VirtualUserManager::checkUser(const char* username, const char* password, llong timestamp) {
	EString u(username);
	QueryUser* user = userMap.get(&u);

	if (user) {
		u.append(user->password).append(timestamp);
		byte pw[ES_MD5_DIGEST_LEN] = {0};
		es_md5_ctx_t c;
		eso_md5_init(&c);
		eso_md5_update(&c, (const unsigned char *)u.c_str(), u.length());
		eso_md5_final((es_uint8_t*)pw, &c);

		if (EString::toHexString(pw, ES_MD5_DIGEST_LEN).equalsIgnoreCase(password)) {
			return user;
		}
	}
	return null;
}

//=============================================================================

DBSoHandleManager::~DBSoHandleManager() {
	soCloseAll();
}
DBSoHandleManager::DBSoHandleManager(EConfig& conf): conf(conf) {
	EConfig* subc = conf.getConfig("DBTYPE");
	if (!subc) {
		throw EIllegalArgumentException(__FILE__, __LINE__, "[DBTYPE] is null.");
	}
}
void DBSoHandleManager::soOpenAll() {
	EConfig* subc = conf.getConfig("DBTYPE");
	EArray<EString*> bdb = subc->keyNames();
	for (int i=0; i<bdb.size(); i++) {
		const char* dbtype = bdb[i]->c_str();
		boolean on = subc->getBoolean(dbtype, false);
		if (on) {
			EString dsofile(ServerConfig::BasePath.c_str());
			dsofile.append("/dblib/")
#ifdef __APPLE__
					.append("/osx/")
#else //__linux__
					.append("/linux/")
#endif
					.append(dbtype).append(".so");
			TLOGERROR("sofile:" << dsofile.c_str() << endl);
			es_dso_t* handle = eso_dso_load(dsofile.c_str());
			if (!handle) {
				throw EFileNotFoundException(__FILE__, __LINE__, dsofile.c_str());
			}
			make_database_t* func = (make_database_t*)eso_dso_sym(handle, "makeDatabase");
			if (!func) {
				eso_dso_unload(&handle);
				throw ERuntimeException(__FILE__, __LINE__, "makeDatabase");
			}

			SoHandle *sh = new SoHandle();
			sh->handle = handle;
			sh->func = func;
			handles.put(new EString(dbtype), sh);
		}
	}
}
void DBSoHandleManager::soCloseAll() {
	sp<EIterator<SoHandle*> > iter = handles.values()->iterator();
	while (iter->hasNext()) {
		SoHandle* sh = iter->next();
		eso_dso_unload(&sh->handle);
	}
	handles.clear();
}
void DBSoHandleManager::testConnectAll() {
	ON_FINALLY_NOTHROW(
		soCloseAll();
	) {
		soOpenAll();

		fprintf(stderr, "virtual database connecting test...");

		VirtualDBManager dbmgr(conf);
		EHashMap<EString*,VirtualDB*>& dbmap = dbmgr.getDBMap();
		sp<EIterator<VirtualDB*> > iter = dbmap.values()->iterator();
		while (iter->hasNext()) {
			VirtualDB* vdb = iter->next();

			sp<EDatabase> db = getDatabase(vdb->dbType.c_str(), null);
			if (db == null) {
				continue;
			}

			boolean r = db->open(vdb->database.c_str(), vdb->host.c_str(),
					vdb->port, vdb->username.c_str(),
					vdb->password.c_str(), vdb->clientEncoding.c_str(),
					vdb->connectTimeout);
			if (!r) {
				EString msg = EString::formatOf("open database [%s] failed: (%s)",
						   vdb->alias_dbname.c_str(), db->getErrorMessage().c_str());
				throw EException(__FILE__, __LINE__, msg.c_str());
			}
			fprintf(stderr, "virtual database [%s] connect success.", vdb->alias_dbname.c_str());
		}
	}}
}
sp<EDatabase> DBSoHandleManager::getDatabase(const char* dbtype, efc::edb::EDBProxyInf* proxy) {
	EString key(dbtype);
	SoHandle *sh = handles.get(&key);
	if (!sh) return null;
	make_database_t* func = sh->func;
	return func(proxy);
}

/////////////////////////////////////////////////////////////////

static edb_pkg_head_t read_edb_pkg_head(const void* buf, int size) {
	ES_ASSERT(size >= sizeof(edb_pkg_head_t));

	edb_pkg_head_t head;
	memcpy(&head, buf, sizeof(head));
	head.reqid = ntohl(head.reqid);
	head.pkglen = ntohl(head.pkglen);
	head.crc32 = ntohl(head.crc32);

	return head;
}

static int edb_pkg_parse(string &in, string &out)
{
    if(in.length() < sizeof(edb_pkg_head_t))
    {
        return TC_EpollServer::PACKET_LESS;
    }

    edb_pkg_head_t head = read_edb_pkg_head(in.c_str(), in.size());

    if (head.magic != 0xEE) {
        return TC_EpollServer::PACKET_ERR;
    }

    ECRC32 crc;
    crc.update((es_int8_t*)in.c_str(), sizeof(edb_pkg_head_t)-sizeof(int32_t));
    if (crc.getValue() != head.crc32) {
		return TC_EpollServer::PACKET_ERR;
	}

    es_int32_t iPkgLen = head.pkglen + sizeof(head);

    if((unsigned int)in.length() < iPkgLen)
    {
        return TC_EpollServer::PACKET_LESS;
    }

    out = in.substr(0, iPkgLen);

    in  = in.substr(iPkgLen);

    return TC_EpollServer::PACKET_FULL;
}


void
DBProxyServer::initialize()
{
    //initialize application here:

    addServant<DBProxyServantImp>(ServerConfig::Application + "." + ServerConfig::ServerName + ".DBProxyServantObj");
    addServantProtocol(ServerConfig::Application + "." + ServerConfig::ServerName + ".DBProxyServantObj", edb_pkg_parse);

	//conf
    std::string ini_path = ServerConfig::BasePath + "/dbproxy.ini";
	conf.loadFromINI(ini_path.c_str());
	if (conf.isEmpty()) {
		throw EIllegalArgumentException(__FILE__, __LINE__);
	}

	somgr = new DBSoHandleManager(conf);
	dbmgr = new VirtualDBManager(conf);
	usrmgr = new VirtualUserManager(conf, *dbmgr);

    //test db connection
    somgr->testConnectAll();

	somgr->soOpenAll();
}
/////////////////////////////////////////////////////////////////
void
DBProxyServer::destroyApp()
{
    //destroy application here:

	somgr->soCloseAll();
}
/////////////////////////////////////////////////////////////////
int
main(int argc, char* argv[])
{
	ESystem::init(argc, (const char **)argv);

    try
    {
        g_app.main(argc, argv);
        g_app.waitForShutdown();
    }
    catch (std::exception& e)
    {
        cerr << "std::exception:" << e.what() << std::endl;
    }
    catch (...)
    {
        cerr << "unknown exception." << std::endl;
    }
    return -1;
}
/////////////////////////////////////////////////////////////////
