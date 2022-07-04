CREATE EXTENSION postgis_toaster;

CREATE TABLE test_postgis_toaster(a bytea STORAGE EXTERNAL TOASTER postgis_toaster,
                                  b text STORAGE PLAIN);

INSERT INTO test_postgis_toaster SELECT repeat('a', 10000)::bytea;
SELECT pg_column_size(a), pg_column_size(b) FROM test_postgis_toaster;

EXPLAIN(ANALYZE, BUFFERS, COSTS OFF, TIMING OFF, SUMMARY OFF)
SELECT substring(a, 1, 48) FROM test_postgis_toaster;
SELECT substring(a, 1, 48) FROM test_postgis_toaster;

EXPLAIN(ANALYZE, BUFFERS, COSTS OFF, TIMING OFF, SUMMARY OFF)
SELECT substring(a, 1, 49) FROM test_postgis_toaster;
SELECT substring(a, 1, 49) FROM test_postgis_toaster;

UPDATE test_postgis_toaster SET b = repeat('b', 1800);
SELECT pg_column_size(a), pg_column_size(b) FROM test_postgis_toaster;

UPDATE test_postgis_toaster SET b = repeat('b', 1950);
SELECT pg_column_size(a), pg_column_size(b) FROM test_postgis_toaster;

UPDATE test_postgis_toaster SET b = repeat('b', 8100);
SELECT pg_column_size(a), pg_column_size(b) FROM test_postgis_toaster;


ALTER TABLE test_postgis_toaster ALTER COLUMN a SET STORAGE EXTENDED;

INSERT INTO test_postgis_toaster SELECT repeat('a', 1000000)::bytea;

EXPLAIN(ANALYZE, BUFFERS, COSTS OFF, TIMING OFF, SUMMARY OFF)
SELECT substring(a, 1, 48) FROM test_postgis_toaster;
SELECT substring(a, 1, 48) FROM test_postgis_toaster;

EXPLAIN(ANALYZE, BUFFERS, COSTS OFF, TIMING OFF, SUMMARY OFF)
SELECT substring(a, 1, 49) FROM test_postgis_toaster;
SELECT substring(a, 1, 49) FROM test_postgis_toaster;


UPDATE test_postgis_toaster SET a = repeat('b', 1000000)::bytea;
UPDATE test_postgis_toaster SET a = repeat('b', 1000000)::bytea;
UPDATE test_postgis_toaster SET a = repeat('b', 1000000)::bytea;
UPDATE test_postgis_toaster SET a = repeat('b', 1000000)::bytea;
UPDATE test_postgis_toaster SET a = repeat('b', 1000000)::bytea;
UPDATE test_postgis_toaster SET a = repeat('b', 1000000)::bytea;
UPDATE test_postgis_toaster SET a = repeat('b', 1000000)::bytea;
UPDATE test_postgis_toaster SET a = repeat('b', 1000000)::bytea;
UPDATE test_postgis_toaster SET a = repeat('b', 1000000)::bytea;
UPDATE test_postgis_toaster SET a = repeat('b', 1000000)::bytea;

\d+
VACUUM FULL test_postgis_toaster;
\d+

DROP TABLE test_postgis_toaster;
