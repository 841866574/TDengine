
from util.log import *
from util.sql import *
from util.cases import *

import platform
import os
if platform.system().lower() == 'windows':
    import tzlocal


class TDTestCase:

    def init(self, conn, logSql):
        tdLog.debug(f"start to excute {__file__}")
        tdSql.init(conn.cursor())

    def run(self):  # sourcery skip: extract-duplicate-method
        tdSql.prepare()
        # get system timezone
        if platform.system().lower() == 'windows':
            time_zone_1 = tzlocal.get_localzone_name()
            time_zone_2 = time.strftime('(UTC, %z)')
            time_zone = time_zone_1 + " " + time_zone_2
        else:
            time_zone_arr = os.popen('timedatectl | grep zone').read().strip().split(':')
            if len(time_zone_arr) > 1:
                time_zone = time_zone_arr[1].lstrip()
            else:
                # possibly in a docker container
                time_zone_1 = os.popen('ls -l /etc/localtime|awk -F/ \'{print $(NF-1) "/" $NF}\'').read().strip()
                time_zone_2 = os.popen('date "+(%Z, %z)"').read().strip()
                time_zone = time_zone_1 + " " + time_zone_2
        print("expected time zone: " + time_zone)

        tdLog.printNoPrefix("==========step1:create tables==========")
        tdSql.execute(
            '''create table if not exists ntb
            (ts timestamp, c1 int, c2 float,c3 double)
            '''
        )
        tdSql.execute(
            '''create table if not exists stb
            (ts timestamp, c1 int, c2 float,c3 double) tags(t0 int)
            '''
        )
        tdSql.execute(
            '''create table if not exists stb_1 using stb tags(100)
            '''
        )

        tdLog.printNoPrefix("==========step2:insert data==========")
        tdSql.execute(
            "insert into ntb values(now,10,99.99,11.111111)(today(),100,11.111,22.222222)")
        tdSql.execute(
            "insert into stb_1 values(now,111,99.99,11.111111)(today(),1,11.111,22.222222)")

        tdLog.printNoPrefix("==========step3:query data==========")
        
        tdSql.query("select timezone() from ntb")
        tdSql.checkRows(2)
        tdSql.checkData(0, 0, time_zone)
        tdSql.query("select timezone() from db.ntb")
        tdSql.checkRows(2)
        tdSql.checkData(0, 0, time_zone)
        tdSql.query("select timezone() from stb")
        tdSql.checkRows(2)
        tdSql.checkData(0, 0, time_zone)
        tdSql.query("select timezone() from db.stb")
        tdSql.checkRows(2)
        tdSql.checkData(0, 0, time_zone)
        tdSql.query("select timezone() from stb_1")
        tdSql.checkRows(2)
        tdSql.checkData(0, 0, time_zone)
        tdSql.query("select timezone() from db.stb_1 ")
        tdSql.checkRows(2)
        tdSql.checkData(0, 0, time_zone)

        tdSql.error("select timezone(1) from stb")
        tdSql.error("select timezone(1) from db.stb")
        tdSql.error("select timezone(1) from ntb")
        tdSql.error("select timezone(1) from db.ntb")
        tdSql.error("select timezone(1) from stb_1")
        tdSql.error("select timezone(1) from db.stb_1")
        tdSql.error("select timezone(now()) from stb")
        tdSql.error("select timezone(now()) from db.stb")

        tdSql.query(f"select * from ntb where timezone()='{time_zone}'")
        tdSql.checkRows(2)
        tdSql.query("select timezone()+1 from ntb")
        tdSql.checkRows(2)
        tdSql.query("select timezone()+1 from db.ntb")
        tdSql.checkRows(2)
        tdSql.query("select timezone()+1 from stb")
        tdSql.checkRows(2)
        tdSql.query("select timezone()+1 from db.stb")
        tdSql.checkRows(2)
        tdSql.query("select timezone()+1 from stb_1")
        tdSql.checkRows(2)
        tdSql.query("select timezone()+1 from db.stb_1")
        tdSql.checkRows(2)
        tdSql.query("select timezone()+1.5 from ntb")
        tdSql.checkRows(2)
        tdSql.query("select timezone()+1.5 from db.ntb")
        tdSql.checkRows(2)
        tdSql.query("select timezone()-100 from ntb")
        tdSql.checkRows(2)
        tdSql.query("select timezone()*100 from ntb")
        tdSql.checkRows(2)
        tdSql.query("select timezone()/10 from ntb")
        # tdSql.query("select timezone()/0 from ntb")


        tdSql.query("select timezone()+null from ntb")
        tdSql.checkRows(2)
        tdSql.checkData(0,0,None)
        tdSql.query("select timezone()-null from ntb")
        tdSql.checkRows(2)
        tdSql.checkData(0,0,None)
        tdSql.query("select timezone()*null from ntb")
        tdSql.checkRows(2)
        tdSql.checkData(0,0,None)
        tdSql.query("select timezone()/null from ntb")
        tdSql.checkRows(2)
        tdSql.checkData(0,0,None)
        # tdSql.query("select timezone()")
        tdSql.query("select timezone()+null from stb")
        tdSql.checkRows(2)
        tdSql.checkData(0,0,None)
        tdSql.query("select timezone()-null from stb")
        tdSql.checkRows(2)
        tdSql.checkData(0,0,None)
        tdSql.query("select timezone()*null from stb")
        tdSql.checkRows(2)
        tdSql.checkData(0,0,None)
        tdSql.query("select timezone()/null from stb")
        tdSql.checkRows(2)
        tdSql.checkData(0,0,None)
        tdSql.query("select timezone()+null from stb_1")
        tdSql.checkRows(2)
        tdSql.checkData(0,0,None)
        tdSql.query("select timezone()-null from stb_1")
        tdSql.checkRows(2)
        tdSql.checkData(0,0,None)
        tdSql.query("select timezone()*null from stb_1")
        tdSql.checkRows(2)
        tdSql.checkData(0,0,None)
        tdSql.query("select timezone()/null from stb_1")
        tdSql.checkRows(2)
        tdSql.checkData(0,0,None)
    def stop(self):
        tdSql.close()
        tdLog.success(f"{__file__} successfully executed")


tdCases.addLinux(__file__, TDTestCase())
tdCases.addWindows(__file__, TDTestCase())
