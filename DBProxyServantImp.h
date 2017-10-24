#ifndef _DBProxyServantImp_H_
#define _DBProxyServantImp_H_

#include "servant/Application.h"

#include "Efc.hh"
#include "./dblib/inc/EDatabase.hh"

class DBProxyServantImp : public  tars::Servant, virtual public efc::edb::EDBProxyInf
{
public:
    /**
     *
     */
    virtual ~DBProxyServantImp() {};

    /**
     *
     */
    virtual void initialize();

    /**
     *
     */
    virtual void destroy();

    /**
	 *
	 */
	virtual int test(tars::TarsCurrentPtr current) { return 0;};

    //重载Servant的doRequest方法
    int doRequest(tars::TarsCurrentPtr current, vector<char>& response);

    //重载Servant的doClose方法
    int doClose(tars::TarsCurrentPtr current);

    virtual EString getProxyVersion();
	virtual void dumpSQL(const char *oldSql, const char *newSql);
};
/////////////////////////////////////////////////////
#endif
