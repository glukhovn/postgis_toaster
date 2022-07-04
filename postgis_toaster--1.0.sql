/* contrib/postgis_toaster/bytea_toaster--1.0.sql */

-- complain if script is sourced in psql, rather than via CREATE EXTENSION
\echo Use "CREATE EXTENSION postgis_toaster" to load this file. \quit

CREATE FUNCTION postgis_toaster_handler(internal)
RETURNS toaster_handler
AS 'MODULE_PATHNAME'
LANGUAGE C;

CREATE TOASTER postgis_toaster  HANDLER postgis_toaster_handler;

COMMENT ON TOASTER postgis_toaster IS 'postgis_toaster is a postgis toaster';
