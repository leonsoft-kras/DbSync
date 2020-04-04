psql -U postgres

create database db1;
create user test_user1 with password 'pass1';
grant all on database db1 to test_user;

create database db2;
create user test_user2 with password 'pass2';
grant all on database db2 to test_user2;

-- ------------------------------------

DROP TRIGGER "TEST_TRG" ON testtab;
DROP FUNCTION "TEST_TRG_FN" ();


DROP TABLE testtab;
CREATE TABLE testtab (
    a1 integer,
    a2 integer,
    a3 char(100),
    a4 varchar(200),
    a5 timestamp(6) without time zone,
    a6 timestamp(6) without time zone DEFAULT CURRENT_TIMESTAMP,
    a7 bytea
)

--
CREATE FUNCTION "TEST_TRG_FN" ()
RETURNS trigger
AS 
$body$
BEGIN
  RETURN NEW;
END;
$body$
LANGUAGE plpgsql;

--
CREATE TRIGGER "TEST_TRG"
    BEFORE INSERT ON testtab
    FOR EACH STATEMENT
    EXECUTE PROCEDURE "TEST_TRG_FN" ();

