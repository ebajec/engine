# linear-algebra
Template (m*n)-matrix class with most standard operations, along with some other tools. Dimensions are template arguments and static during runtime.  However, operations which would result in a change in size still work and are evaluted at compile time.  I made it like this to maximize efficieny when doing small matrix operations on the CPU.   

**Includes:**
  - Standard matrix arithmetic: addition, multiplication, inversion, etc
  - Common matrix operations: det(), adj(), ker(), transpose (need to add special case for complex matrices),
  - Gaussian elimination and solving systems of equations
  - A few vector operations
  - R^3 rotation matrices, rotation about an axis
