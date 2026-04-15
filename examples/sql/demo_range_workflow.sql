-- range query demo workflow
INSERT INTO demo.students (name, major, grade) VALUES ('Gina', 'AI', 'B');
INSERT INTO demo.students (name, major, grade) VALUES ('Hank', 'Data', 'A');
INSERT INTO demo.students (name, major, grade) VALUES ('Iris', 'DB', 'A');
INSERT INTO demo.students (name, major, grade) VALUES ('Joon', 'System', 'B');

SELECT id, name, major FROM demo.students WHERE id >= 4;
SELECT id, name FROM demo.students WHERE id <= 5;
SELECT id, name FROM demo.students WHERE id > 6;
SELECT id, name FROM demo.students WHERE id < 8;
