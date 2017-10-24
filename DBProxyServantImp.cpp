#include "DBProxyServantImp.h"
#include "DBProxyServer.h"
#include "./interface/EDBInterface.h"

using namespace std;

//////////////////////////////////////////////////////
void DBProxyServantImp::initialize()
{
    //initialize servant here:
	EThread::c_init();
}

//////////////////////////////////////////////////////
void DBProxyServantImp::destroy()
{
    //destroy servant here:

}

static edb_pkg_head_t read_edb_pkg_head(const void* buf, int size) {
	ES_ASSERT(size >= sizeof(edb_pkg_head_t));

	edb_pkg_head_t head;
	memcpy(&head, buf, sizeof(head));
	head.reqid = ntohl(head.reqid);
	head.pkglen = ntohl(head.pkglen);
	head.crc32 = ntohl(head.crc32);

	return head;
}

static void encodeFailedResponse(vector<char>& buffer, const char* errmsg, uint reqid) {
	sp<EBson> rep = new EBson();
	rep->addInt(EDB_KEY_ERRCODE, ES_FAILURE);
	rep->add(EDB_KEY_ERRMSG, errmsg);
	EByteBuffer out;
	rep->Export(&out, null, false);

	edb_pkg_head_t pkg_head;
	memset(&pkg_head, 0, sizeof(pkg_head));
	pkg_head.magic = 0xEE;
	pkg_head.type = 0;
	pkg_head.gzip = 0;
	pkg_head.next = 0;
	pkg_head.pkglen = htonl(out.size());
	pkg_head.reqid = htonl(reqid);

	ECRC32 crc32;
	crc32.update((byte*)&pkg_head, sizeof(pkg_head)-sizeof(pkg_head.crc32));
	pkg_head.crc32 = htonl(crc32.getValue());

	buffer.clear();
	buffer.resize(sizeof(pkg_head) + out.size());
	memcpy(&buffer[0], &pkg_head, sizeof(pkg_head));
	memcpy(&buffer[0] + sizeof(pkg_head), out.data(), out.size());
}

int DBProxyServantImp::doRequest(tars::TarsCurrentPtr current, vector<char>& response)
{
	const vector<char>& request = current->getRequestBuffer();

	//get tars request id
	edb_pkg_head_t req_head = read_edb_pkg_head(request.data(), request.size());

	//req
	EBson req;
	EByteArrayInputStream bais((char*)request.data() + sizeof(edb_pkg_head_t), request.size() - sizeof(edb_pkg_head_t));
	if (req_head.gzip) {
		EGZIPInputStream gis(&bais);
		EBsonParser bp(&gis);
		bp.nextBson(&req);
	} else {
		EBsonParser bp(&bais);
		bp.nextBson(&req);
	}

	int fd = current->getFd();
	sp<EDatabase> database = g_app.dbmap.get(fd);

	if (req.getInt(EDB_KEY_MSGTYPE) == DB_SQL_DBOPEN) {
		EString username = req.getString(EDB_KEY_USERNAME);
		QueryUser* user = g_app.usrmgr->checkUser(
				username.c_str(),
				req.getString(EDB_KEY_PASSWORD).c_str(),
				req.getLLong(EDB_KEY_TIMESTAMP));
		if (!user) {
			EString msg = "username or password error.";
			LOG->warn() << msg.c_str() << endl;
			encodeFailedResponse(response, msg.c_str(), req_head.reqid);
			return 0;
		}
		VirtualDB* vdb = user->virdb;
		if (!vdb) {
			EString msg("no database access permission.");
			LOG->warn() << msg.c_str() << endl;
			encodeFailedResponse(response, msg.c_str(), req_head.reqid);
			return 0;
		}
		database = g_app.somgr->getDatabase(vdb->dbType.c_str(), this);
		if (database == null) {
			EString msg = EString::formatOf("database %s is closed.", vdb->dbType.c_str());
			LOG->warn() << msg.c_str() << endl;
			encodeFailedResponse(response, msg.c_str(), req_head.reqid);
			return 0;
		}

		//!
		g_app.dbmap.put(fd, database);

		//update some field to real data.
		req.set(EDB_KEY_HOST, vdb->host.c_str());
		req.setInt(EDB_KEY_PORT, vdb->port);
		req.set(EDB_KEY_USERNAME, vdb->username.c_str());
		req.set(EDB_KEY_PASSWORD, vdb->password.c_str());
	}

	if (database == null) {
		encodeFailedResponse(response, "unknown error.", req_head.reqid);
		return 0;
	}

	//rep
	sp<EBson> rep = database->processSQL(&req, null);

	ES_ASSERT(rep != null);
	EByteBuffer out;
	rep->Export(&out, null, false);

	edb_pkg_head_t pkg_head;
	memset(&pkg_head, 0, sizeof(pkg_head));
	pkg_head.magic = 0xEE;
	pkg_head.type = 0;
	pkg_head.gzip = (out.size() > 512) ? 1 : 0;
	pkg_head.next = 0;
	pkg_head.reqid = htonl(req_head.reqid);
	if (pkg_head.gzip) {
		EByteArrayOutputStream baos;
		EGZIPOutputStream gos(&baos);
		gos.write(out.data(), out.size());
		gos.finish();
		pkg_head.pkglen = htonl(baos.size());

		ECRC32 crc32;
		crc32.update((byte*)&pkg_head, sizeof(pkg_head)-sizeof(pkg_head.crc32));
		pkg_head.crc32 = htonl(crc32.getValue());

		response.resize(sizeof(pkg_head) + baos.size());
		memcpy(&response[0], &pkg_head, sizeof(pkg_head));
		memcpy(&response[0] + sizeof(pkg_head), baos.data(), baos.size());
	} else {
		pkg_head.pkglen = htonl(out.size());

		ECRC32 crc32;
		crc32.update((byte*)&pkg_head, sizeof(pkg_head)-sizeof(pkg_head.crc32));
		pkg_head.crc32 = htonl(crc32.getValue());

		response.resize(sizeof(pkg_head) + out.size());
		memcpy(&response[0], &pkg_head, sizeof(pkg_head));
		memcpy(&response[0] + sizeof(pkg_head), out.data(), out.size());
	}

	return 0;
}
//客户端关闭到服务端的连接，或者服务端发现客户端长时间未发送包过来，然后超过60s就关闭连接
//调用的方法
int DBProxyServantImp::doClose(TarsCurrentPtr current)
{
	int fd = current->getFd();
	sp<EDatabase> database = g_app.dbmap.get(fd);
	if (database != null) {
		database->close();
		LOG->info() << "session client closed." << endl ;
	}

	return 0;
}

EString DBProxyServantImp::getProxyVersion()
{
	return "0.1.0";
}

void DBProxyServantImp::dumpSQL(const char *oldSql, const char *newSql)
{
	if (oldSql || newSql) {
		LOG->debug() << "SQL: " << (newSql ? newSql : oldSql) << endl;
	}
}
