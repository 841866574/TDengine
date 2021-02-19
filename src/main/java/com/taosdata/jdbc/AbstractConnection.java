package com.taosdata.jdbc;

import java.sql.*;
import java.util.Enumeration;
import java.util.Map;
import java.util.Properties;
import java.util.concurrent.Executor;

public abstract class AbstractConnection extends WrapperImpl implements Connection {

    protected volatile boolean isClosed;
    protected volatile String catalog;
    protected volatile Properties clientInfoProps = new Properties();

    @Override
    public abstract Statement createStatement() throws SQLException;

    @Override
    public abstract PreparedStatement prepareStatement(String sql) throws SQLException;

    @Override
    public CallableStatement prepareCall(String sql) throws SQLException {
        if (isClosed())
            throw TSDBError.createSQLException(TSDBErrorNumbers.ERROR_CONNECTION_CLOSED);
        throw TSDBError.createSQLException(TSDBErrorNumbers.ERROR_UNSUPPORTED_METHOD);
    }

    @Override
    public String nativeSQL(String sql) throws SQLException {
        if (isClosed())
            throw TSDBError.createSQLException(TSDBErrorNumbers.ERROR_CONNECTION_CLOSED);
        // do nothing
        return sql;
    }

    @Override
    public void setAutoCommit(boolean autoCommit) throws SQLException {
        if (isClosed())
            throw TSDBError.createSQLException(TSDBErrorNumbers.ERROR_CONNECTION_CLOSED);

        //do nothing
    }

    @Override
    public boolean getAutoCommit() throws SQLException {
        if (isClosed())
            throw TSDBError.createSQLException(TSDBErrorNumbers.ERROR_CONNECTION_CLOSED);

        return true;
    }

    @Override
    public void commit() throws SQLException {
        if (isClosed())
            throw TSDBError.createSQLException(TSDBErrorNumbers.ERROR_CONNECTION_CLOSED);

        // do nothing
    }

    @Override
    public void rollback() throws SQLException {
        if (isClosed())
            throw TSDBError.createSQLException(TSDBErrorNumbers.ERROR_CONNECTION_CLOSED);

        // do nothing
    }

    @Override
    public abstract void close() throws SQLException;

    @Override
    public abstract boolean isClosed() throws SQLException;

    @Override
    public abstract DatabaseMetaData getMetaData() throws SQLException;

    @Override
    public void setReadOnly(boolean readOnly) throws SQLException {
        if (isClosed())
            throw TSDBError.createSQLException(TSDBErrorNumbers.ERROR_CONNECTION_CLOSED);

        //do nothing
    }

    @Override
    public boolean isReadOnly() throws SQLException {
        if (isClosed())
            throw TSDBError.createSQLException(TSDBErrorNumbers.ERROR_CONNECTION_CLOSED);
        return true;
    }

    @Override
    public void setCatalog(String catalog) throws SQLException {
        if (isClosed())
            throw TSDBError.createSQLException(TSDBErrorNumbers.ERROR_CONNECTION_CLOSED);

        this.catalog = catalog;
    }

    @Override
    public String getCatalog() throws SQLException {
        if (isClosed())
            throw TSDBError.createSQLException(TSDBErrorNumbers.ERROR_CONNECTION_CLOSED);

        return this.catalog;
    }

    @Override
    public void setTransactionIsolation(int level) throws SQLException {
        if (isClosed())
            throw TSDBError.createSQLException(TSDBErrorNumbers.ERROR_CONNECTION_CLOSED);
        //do nothing
    }

    @Override
    public int getTransactionIsolation() throws SQLException {
        if (isClosed())
            throw TSDBError.createSQLException(TSDBErrorNumbers.ERROR_CONNECTION_CLOSED);

        return Connection.TRANSACTION_NONE;
    }

    @Override
    public SQLWarning getWarnings() throws SQLException {
        if (isClosed())
            throw TSDBError.createSQLException(TSDBErrorNumbers.ERROR_CONNECTION_CLOSED);

        return null;
    }

    @Override
    public void clearWarnings() throws SQLException {
        if (isClosed())
            throw TSDBError.createSQLException(TSDBErrorNumbers.ERROR_CONNECTION_CLOSED);

        // do nothing
    }

    @Override
    public abstract Statement createStatement(int resultSetType, int resultSetConcurrency) throws SQLException;

    @Override
    public abstract PreparedStatement prepareStatement(String sql, int resultSetType, int resultSetConcurrency) throws SQLException;

    @Override
    public CallableStatement prepareCall(String sql, int resultSetType, int resultSetConcurrency) throws SQLException {
        if (isClosed())
            throw TSDBError.createSQLException(TSDBErrorNumbers.ERROR_CONNECTION_CLOSED);

        throw TSDBError.createSQLException(TSDBErrorNumbers.ERROR_UNSUPPORTED_METHOD);
    }

    @Override
    public Map<String, Class<?>> getTypeMap() throws SQLException {
        if (isClosed())
            throw TSDBError.createSQLException(TSDBErrorNumbers.ERROR_CONNECTION_CLOSED);

        throw TSDBError.createSQLException(TSDBErrorNumbers.ERROR_UNSUPPORTED_METHOD);
    }

    @Override
    public void setTypeMap(Map<String, Class<?>> map) throws SQLException {
        if (isClosed())
            throw TSDBError.createSQLException(TSDBErrorNumbers.ERROR_CONNECTION_CLOSED);

        throw TSDBError.createSQLException(TSDBErrorNumbers.ERROR_UNSUPPORTED_METHOD);
    }

    @Override
    public void setHoldability(int holdability) throws SQLException {
        if (isClosed())
            throw TSDBError.createSQLException(TSDBErrorNumbers.ERROR_CONNECTION_CLOSED);
        //do nothing
    }

    @Override
    public int getHoldability() throws SQLException {
        if (isClosed())
            throw TSDBError.createSQLException(TSDBErrorNumbers.ERROR_CONNECTION_CLOSED);

        return ResultSet.HOLD_CURSORS_OVER_COMMIT;
    }

    @Override
    public Savepoint setSavepoint() throws SQLException {
        if (isClosed())
            throw TSDBError.createSQLException(TSDBErrorNumbers.ERROR_CONNECTION_CLOSED);

        throw TSDBError.createSQLException(TSDBErrorNumbers.ERROR_UNSUPPORTED_METHOD);
    }

    @Override
    public Savepoint setSavepoint(String name) throws SQLException {
        if (isClosed())
            throw TSDBError.createSQLException(TSDBErrorNumbers.ERROR_CONNECTION_CLOSED);

        throw TSDBError.createSQLException(TSDBErrorNumbers.ERROR_UNSUPPORTED_METHOD);
    }

    @Override
    public void rollback(Savepoint savepoint) throws SQLException {
        if (isClosed())
            throw TSDBError.createSQLException(TSDBErrorNumbers.ERROR_CONNECTION_CLOSED);

        throw TSDBError.createSQLException(TSDBErrorNumbers.ERROR_UNSUPPORTED_METHOD);
    }

    @Override
    public void releaseSavepoint(Savepoint savepoint) throws SQLException {
        if (isClosed())
            throw TSDBError.createSQLException(TSDBErrorNumbers.ERROR_CONNECTION_CLOSED);

        throw TSDBError.createSQLException(TSDBErrorNumbers.ERROR_UNSUPPORTED_METHOD);
    }

    @Override
    public abstract Statement createStatement(int resultSetType, int resultSetConcurrency, int resultSetHoldability) throws SQLException;

    @Override
    public abstract PreparedStatement prepareStatement(String sql, int resultSetType, int resultSetConcurrency, int resultSetHoldability) throws SQLException;

    @Override
    public CallableStatement prepareCall(String sql, int resultSetType, int resultSetConcurrency, int resultSetHoldability) throws SQLException {
        if (isClosed())
            throw TSDBError.createSQLException(TSDBErrorNumbers.ERROR_CONNECTION_CLOSED);

        throw TSDBError.createSQLException(TSDBErrorNumbers.ERROR_UNSUPPORTED_METHOD);
    }

    @Override
    public PreparedStatement prepareStatement(String sql, int autoGeneratedKeys) throws SQLException {
        if (isClosed())
            throw TSDBError.createSQLException(TSDBErrorNumbers.ERROR_CONNECTION_CLOSED);
        switch (autoGeneratedKeys) {
            case Statement.RETURN_GENERATED_KEYS:
                throw TSDBError.createSQLException(TSDBErrorNumbers.ERROR_UNSUPPORTED_METHOD);
            case Statement.NO_GENERATED_KEYS:
                break;
        }

        return prepareStatement(sql, ResultSet.TYPE_FORWARD_ONLY, ResultSet.CONCUR_READ_ONLY);
    }

    @Override
    public PreparedStatement prepareStatement(String sql, int[] columnIndexes) throws SQLException {
        if (isClosed())
            throw TSDBError.createSQLException(TSDBErrorNumbers.ERROR_CONNECTION_CLOSED);

        throw TSDBError.createSQLException(TSDBErrorNumbers.ERROR_UNSUPPORTED_METHOD);
    }

    @Override
    public PreparedStatement prepareStatement(String sql, String[] columnNames) throws SQLException {
        if (isClosed())
            throw TSDBError.createSQLException(TSDBErrorNumbers.ERROR_CONNECTION_CLOSED);

        throw TSDBError.createSQLException(TSDBErrorNumbers.ERROR_UNSUPPORTED_METHOD);
    }

    @Override
    public Clob createClob() throws SQLException {
        if (isClosed())
            throw TSDBError.createSQLException(TSDBErrorNumbers.ERROR_CONNECTION_CLOSED);

        throw TSDBError.createSQLException(TSDBErrorNumbers.ERROR_UNSUPPORTED_METHOD);
    }

    @Override
    public Blob createBlob() throws SQLException {
        if (isClosed())
            throw TSDBError.createSQLException(TSDBErrorNumbers.ERROR_CONNECTION_CLOSED);

        throw TSDBError.createSQLException(TSDBErrorNumbers.ERROR_UNSUPPORTED_METHOD);
    }

    @Override
    public NClob createNClob() throws SQLException {
        if (isClosed())
            throw TSDBError.createSQLException(TSDBErrorNumbers.ERROR_CONNECTION_CLOSED);

        throw TSDBError.createSQLException(TSDBErrorNumbers.ERROR_UNSUPPORTED_METHOD);
    }

    @Override
    public SQLXML createSQLXML() throws SQLException {
        if (isClosed())
            throw TSDBError.createSQLException(TSDBErrorNumbers.ERROR_CONNECTION_CLOSED);

        throw TSDBError.createSQLException(TSDBErrorNumbers.ERROR_UNSUPPORTED_METHOD);
    }

    @Override
    public abstract boolean isValid(int timeout) throws SQLException;

    @Override
    public void setClientInfo(String name, String value) throws SQLClientInfoException {
        if (isClosed)
            throw TSDBError.createSQLClientInfoException(TSDBErrorNumbers.ERROR_CONNECTION_CLOSED);

        if (clientInfoProps == null)
            clientInfoProps = new Properties();
        clientInfoProps.setProperty(name, value);
    }

    @Override
    public void setClientInfo(Properties properties) throws SQLClientInfoException {
        if (isClosed)
            throw TSDBError.createSQLClientInfoException(TSDBErrorNumbers.ERROR_CONNECTION_CLOSED);

        for (Enumeration<Object> enumer = properties.keys(); enumer.hasMoreElements(); ) {
            String name = (String) enumer.nextElement();
            clientInfoProps.put(name, properties.getProperty(name));
        }
    }

    @Override
    public String getClientInfo(String name) throws SQLException {
        if (isClosed)
            throw TSDBError.createSQLClientInfoException(TSDBErrorNumbers.ERROR_CONNECTION_CLOSED);

        return clientInfoProps.getProperty(name);
    }

    @Override
    public Properties getClientInfo() throws SQLException {
        if (isClosed)
            throw TSDBError.createSQLClientInfoException(TSDBErrorNumbers.ERROR_CONNECTION_CLOSED);

        return clientInfoProps;
    }

    @Override
    public Array createArrayOf(String typeName, Object[] elements) throws SQLException {
        if (isClosed())
            throw TSDBError.createSQLException(TSDBErrorNumbers.ERROR_CONNECTION_CLOSED);

        throw TSDBError.createSQLException(TSDBErrorNumbers.ERROR_UNSUPPORTED_METHOD);
    }

    @Override
    public Struct createStruct(String typeName, Object[] attributes) throws SQLException {
        if (isClosed())
            throw TSDBError.createSQLException(TSDBErrorNumbers.ERROR_CONNECTION_CLOSED);

        throw TSDBError.createSQLException(TSDBErrorNumbers.ERROR_UNSUPPORTED_METHOD);
    }

    @Override
    public void setSchema(String schema) throws SQLException {
        if (isClosed())
            throw TSDBError.createSQLException(TSDBErrorNumbers.ERROR_CONNECTION_CLOSED);
        //do nothing
    }

    @Override
    public String getSchema() throws SQLException {
        if (isClosed())
            throw TSDBError.createSQLException(TSDBErrorNumbers.ERROR_CONNECTION_CLOSED);

        return null;
    }

    @Override
    public void abort(Executor executor) throws SQLException {
        if (isClosed())
            throw TSDBError.createSQLException(TSDBErrorNumbers.ERROR_CONNECTION_CLOSED);

        // do nothing
    }

    @Override
    public void setNetworkTimeout(Executor executor, int milliseconds) throws SQLException {
        if (isClosed())
            throw TSDBError.createSQLException(TSDBErrorNumbers.ERROR_CONNECTION_CLOSED);
        if (milliseconds < 0)
            throw TSDBError.createSQLException(TSDBErrorNumbers.ERROR_INVALID_VARIABLE);

        throw TSDBError.createSQLException(TSDBErrorNumbers.ERROR_UNSUPPORTED_METHOD);
    }

    @Override
    public int getNetworkTimeout() throws SQLException {
        if (isClosed())
            throw TSDBError.createSQLException(TSDBErrorNumbers.ERROR_CONNECTION_CLOSED);

        throw TSDBError.createSQLException(TSDBErrorNumbers.ERROR_UNSUPPORTED_METHOD);
    }
}
