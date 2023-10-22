# Effaln
version 1.0（20220915）   
version 2.0 (20231022)

# What is it?
Effaln is an efficient Burrows-Wheeler-based mapper for longer Next-generation sequencing reads. 

# How to use it?
Effaln consists of two components, index building and read mapping. You should first build the FM-index with the reference genome, and then perform the mapping process.

# Step I. Install
  1. Download (or clone) the source code form https://github.com/guolilu3/Effaln
  2. Compile the source code. (Note that you need to compile semiWFA first)

# Step II. Build FM-index
  1. Run the shell command: "./effaln-index \<refName\> \<idxName\>", where refName is the reference genome, idxName is the index file name.
  
# Step III. Mapping
  1. Run the shell command: "./effaln -x \<idxName\> -U \<rdsName\> -S \<samName\>", where idxName is the index file name, rdsName is the sequencing reads file name, samName is the mapping result file name.
  
# Feedback
Please report bugs to Email: guolilu@stu.xidian.edu.cn if any questions or suggestions. Your feedback and test results are welcome.
