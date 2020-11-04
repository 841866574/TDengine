package com.taosdata.jdbc;

import org.junit.Before;
import org.junit.BeforeClass;
import org.junit.Test;

import java.sql.*;
import java.util.Properties;

import static org.junit.Assert.*;

public class TSDBDriverTest {

    private static String[] validURLs = {
            "jdbc:TAOS://localhost:0",
            "jdbc:TAOS://localhost",
            "jdbc:TAOS://localhost:6030/test",
            "jdbc:TAOS://localhost:6030",
            "jdbc:TAOS://localhost:6030/",
            "jdbc:TSDB://localhost:6030",
            "jdbc:TSDB://localhost:6030/",
            "jdbc:TAOS://127.0.0.1:0/db?user=root&password=taosdata",
            "jdbc:TAOS://:",
            "jdbc:TAOS://:/",
            "jdbc:TAOS://:/test",
            "jdbc:TAOS://localhost:0/?user=root&password=taosdata"
    };
    private static boolean islibLoaded;

    @BeforeClass
    public static void before() {
        String osName = System.getProperty("os.name").toLowerCase();
        String libPath = null;
        switch (osName) {
            case "linux":
                libPath = "/usr/lib/libtaos.so";
                break;
            case "windows":
                libPath = "C:\\TDengine\\driver\\taos.dll";
                break;
            default:
        }
        if (libPath == null)
            return;
        try {
            System.loadLibrary(libPath);
        } catch (UnsatisfiedLinkError error) {
            islibLoaded = false;
        }
        islibLoaded = true;
    }

    @Test
    public void testParseURL() {
        TSDBDriver driver = new TSDBDriver();

        String url = "jdbc:TAOS://127.0.0.1:0/db?user=root&password=taosdata&charset=UTF-8";
        Properties config = new Properties();
        Properties actual = driver.parseURL(url, config);
        assertEquals("failure - host should be 127.0.0.1", "127.0.0.1", actual.get("host"));
        assertEquals("failure - port should be 0", "0", actual.get("port"));
        assertEquals("failure - dbname should be db", "db", actual.get("dbname"));
        assertEquals("failure - user should be root", "root", actual.get("user"));
        assertEquals("failure - password should be taosdata", "taosdata", actual.get("password"));
        assertEquals("failure - charset should be UTF-8", "UTF-8", actual.get("charset"));

        url = "jdbc:TAOS://127.0.0.1:0";
        config = new Properties();
        actual = driver.parseURL(url, config);
        assertEquals("failure - host should be 127.0.0.1", "127.0.0.1", actual.getProperty("host"));
        assertEquals("failure - port should be 0", "0", actual.get("port"));
        assertEquals("failure - dbname should be null", null, actual.get("dbname"));

        url = "jdbc:TAOS://127.0.0.1:0/db";
        config = new Properties();
        actual = driver.parseURL(url, config);
        assertEquals("failure - host should be 127.0.0.1", "127.0.0.1", actual.getProperty("host"));
        assertEquals("failure - port should be 0", "0", actual.get("port"));
        assertEquals("failure - dbname should be db", "db", actual.get("dbname"));

        url = "jdbc:TAOS://:/?";
        config = new Properties();
        config.setProperty(TSDBDriver.PROPERTY_KEY_USER, "root");
        config.setProperty(TSDBDriver.PROPERTY_KEY_PASSWORD, "taosdata");
        actual = driver.parseURL(url, config);
        assertEquals("failure - user should be root", "root", actual.getProperty("user"));
        assertEquals("failure - password should be taosdata", "taosdata", actual.getProperty("password"));
        assertEquals("failure - host should be null", null, actual.getProperty("host"));
        assertEquals("failure - port should be null", null, actual.getProperty("port"));
        assertEquals("failure - dbname should be null", null, actual.getProperty("dbname"));
    }

    @Test
    public void testConnectWithJdbcURL() {

        final String url = "jdbc:TAOS://localhost:3306/log?user=root&password=taosdata";
        try {
            if (islibLoaded) {
                Connection conn = DriverManager.getConnection(url);
                assertNotNull("failure - connection should not be null", conn);
            }
        } catch (SQLException e) {
            e.printStackTrace();
        }
    }

    @Test
    public void testConnectWithProperties() {
        final String jdbcUrl = "jdbc:TAOS://localhost.com:6030/test?user=root&password=taosdata";
        Properties connProps = new Properties();
        connProps.setProperty(TSDBDriver.PROPERTY_KEY_CHARSET, "UTF-8");
        connProps.setProperty(TSDBDriver.PROPERTY_KEY_LOCALE, "en_US.UTF-8");
        connProps.setProperty(TSDBDriver.PROPERTY_KEY_TIME_ZONE, "UTC-8");
        try {
            if (islibLoaded) {
                Connection conn = DriverManager.getConnection(jdbcUrl, connProps);
                assertNotNull("failure - connection should not be null", conn);
            }
        } catch (SQLException e) {
            e.printStackTrace();
        }
    }

    @Test
    public void testConnectWithConfigFile() {
        String jdbcUrl = "jdbc:TAOS://:/test?user=root&password=taosdata";
        Properties connProps = new Properties();
        connProps.setProperty(TSDBDriver.PROPERTY_KEY_CHARSET, "UTF-8");
        connProps.setProperty(TSDBDriver.PROPERTY_KEY_LOCALE, "en_US.UTF-8");
        connProps.setProperty(TSDBDriver.PROPERTY_KEY_TIME_ZONE, "UTC-8");
        try {
            if (islibLoaded) {
                Connection conn = DriverManager.getConnection(jdbcUrl, connProps);
                assertNotNull("failure - connection should not be null", conn);
            }
        } catch (SQLException e) {
            e.printStackTrace();
        }
    }

    @Test(expected = SQLException.class)
    public void testAcceptsURL() throws SQLException {
        Driver driver = new TSDBDriver();
        for (String url : validURLs) {
            assertTrue("failure - acceptsURL(\" " + url + " \") should be true", driver.acceptsURL(url));
        }
        new TSDBDriver().acceptsURL(null);
        fail("acceptsURL throws exception when parameter is null");
    }

    @Test
    public void testGetPropertyInfo() throws SQLException {
        Driver driver = new TSDBDriver();
        final String url = "jdbc:TAOS://localhost:6030/log?user=root&password=taosdata";
        Properties connProps = new Properties();
        connProps.setProperty(TSDBDriver.PROPERTY_KEY_CHARSET, "UTF-8");
        connProps.setProperty(TSDBDriver.PROPERTY_KEY_LOCALE, "en_US.UTF-8");
        connProps.setProperty(TSDBDriver.PROPERTY_KEY_TIME_ZONE, "UTC-8");
        DriverPropertyInfo[] propertyInfo = driver.getPropertyInfo(url, connProps);
        for (DriverPropertyInfo info : propertyInfo) {
            if (info.name.equals(TSDBDriver.PROPERTY_KEY_HOST))
                assertEquals("failure - host should be localhost", "localhost", info.value);
            if (info.name.equals(TSDBDriver.PROPERTY_KEY_PORT))
                assertEquals("failure - port should be 6030", "6030", info.value);
            if (info.name.equals(TSDBDriver.PROPERTY_KEY_DBNAME))
                assertEquals("failure - dbname should be test", "log", info.value);
            if (info.name.equals(TSDBDriver.PROPERTY_KEY_USER))
                assertEquals("failure - user should be root", "root", info.value);
            if (info.name.equals(TSDBDriver.PROPERTY_KEY_PASSWORD))
                assertEquals("failure - password should be root", "taosdata", info.value);
        }
    }

    @Test
    public void testGetMajorVersion() throws SQLException {
        Driver driver = new TSDBDriver();
        assertEquals("failure - getMajorVersion should be 2", 2, driver.getMajorVersion());
    }

    @Test
    public void testGetMinorVersion() {
        Driver driver = new TSDBDriver();
        assertEquals("failure - getMinorVersion should be 0", 0, driver.getMinorVersion());
    }

    @Test
    public void testJdbcCompliant() {
//        assertFalse("failure - jdbcCompliant should be false", new TSDBDriver().jdbcCompliant());
    }

    @Test
    public void testGetParentLogger() throws SQLFeatureNotSupportedException {
        assertNull("failure - getParentLogger should be be null", new TSDBDriver().getParentLogger());
    }
}