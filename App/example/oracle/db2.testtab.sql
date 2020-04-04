drop   table TESTTAB
create table TESTTAB
(
  a1 NUMBER(4),
  a2 NUMBER(4),
  a3 VARCHAR2(100),
  a4 VARCHAR2(200),
  a5 TIMESTAMP(6),
  a6 TIMESTAMP(6) default sysdate,
  a7 BLOB
);

create or replace TRIGGER TEST_TRG before
INSERT ON TESTTAB FOR EACH ROW
DECLARE  VERS NUMBER;
  BEGIN
    SELECT COUNT(*)
    INTO   VERS
    FROM   TESTTAB;
  END;
/

