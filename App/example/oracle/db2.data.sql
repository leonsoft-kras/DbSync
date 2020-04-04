delete from testtab;

-- 1 coincides
   insert into testtab (a1,a2,a3,a4,a5,a7) values (348, 1, 'qwert', '15', to_timestamp('12.12.2012 13:01:17','dd.mm.yyyy hh24:mi:ss'), hextoraw('453d7a34'));
   insert into testtab (a1,a2,a3,a4,a5,a7) values (348, 2, 'bbeer', '25', to_timestamp('12.12.2012 13:02:17','dd.mm.yyyy hh24:mi:ss'), hextoraw('11237a34'));
   insert into testtab (a1,a2,a3,a4,a5,a7) values (348, 3, 'fg34t', '33', to_timestamp('12.12.2012 13:03:17','dd.mm.yyyy hh24:mi:ss'), hextoraw('45453233'));

-- 2 missed
-- insert into testtab (a1,a2,a3,a4,a5,a7) values (348, 4, 'ppvvw', '44', to_timestamp('12.12.2012 13:04:00','dd.mm.yyyy hh24:mi:ss'), hextoraw('45000434'));

-- 3 unwanted
   insert into testtab (a1,a2,a3,a4,a5,a7) values (348,16, 'zzzzz', '11', to_timestamp('12.12.2012 13:04:00','dd.mm.yyyy hh24:mi:ss'), hextoraw('45000434'));

-- 4 different
   insert into testtab (a1,a2,a3,a4,a5,a7) values (348,22, 'ppvvw', '16', to_timestamp('10.10.2012 13:05:00','dd.mm.yyyy hh24:mi:ss'), hextoraw('11111222'));
   
commit;
