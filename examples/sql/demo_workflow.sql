-- multi statement file
INSERT INTO demo.students (id, name, major, grade) VALUES (2, 'Bob', 'AI', 'B');
INSERT INTO demo.students (id, name, major, grade) VALUES (3, 'Choi', 'Data', 'A');
SELECT * FROM demo.students;
SELECT name, grade FROM demo.students WHERE id = 2;
