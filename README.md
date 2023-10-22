# Effaln
version 1.0（20220915）

# What is it?
Effaln is an efficient FM-index-based aligner for mapping DNA sequencing reads. 

# How to use it?
Effaln consists of two components, index building and reads mapping. You should first build the FM-index on the referene genome, then perform the mapping processing.

# Step I. Install
  1. Download (or clone) the source code form https://github.com/Hongweihuo-Lab/Effaln
  2. Compile the source code. (Note that you need to compile semiWFA first)

# Step II. Build FM-index
  1. Run the shell command: "./effaln-index \<refName\> \<idxName\>", where refName is the reference genome, idxName is the index file name.
  
# Step III. Mapping Processig
  1. Run the shell command: "./effaln -x \<idxName\> -U \<rdsName\> -S \<samName\>", where idxName is the index file name, rdsName is the sequencing reads file name, samName is the mapping result file name.
  
# Feedback
Please report bugs to Email: guolilu@stu.xidian.edu.cn if any questions and suggestions. Your feedback and test results are welcome.
