package com.taosdata.jdbc.cases;

import org.junit.Test;

import java.sql.DriverManager;
import java.sql.SQLException;

public class ConnectWrongDatabaseTest {

    @Test
    public void connect() {
        try {
            Class.forName("com.taosdata.jdbc.TSDBDriver");
            DriverManager.getConnection("jdbc:TAOS://localhost:6030/wrong_db?user=root&password=taosdata");
        } catch (ClassNotFoundException e) {
            e.printStackTrace();
        } catch (SQLException e) {
            System.out.println(e.getMessage());
//            e.printStackTrace();
        }
    }

}
