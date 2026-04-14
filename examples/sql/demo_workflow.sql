-- multi statement file
INSERT INTO demo.students (name, major, grade) VALUES ('Bob', 'AI', 'B');
INSERT INTO demo.students (name, major, grade) VALUES ('Choi', 'Data', 'A');
SELECT * FROM demo.students;
SELECT name, grade FROM demo.students WHERE id = 2;
