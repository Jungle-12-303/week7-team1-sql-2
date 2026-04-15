-- multi statement file
INSERT INTO demo.students (name, major, grade) VALUES ('Bob', 'AI', 'B');
INSERT INTO demo.students (name, major, grade) VALUES ('Choi', 'Data', 'A');
INSERT INTO demo.students (name, major, grade) VALUES ('Dana', 'System', 'A');
INSERT INTO demo.students (name, major, grade) VALUES ('Evan', 'AI', 'B');
INSERT INTO demo.students (name, major, grade) VALUES ('Faye', 'DB', 'A');
SELECT * FROM demo.students;
SELECT name, grade FROM demo.students WHERE id = 2;
SELECT id, name FROM demo.students WHERE id >= 4;
