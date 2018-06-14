# Syntax For Todo.txt
Here we document the rules and syntax that user should follow while
manually editing his/her todo.txt. The rules are based on general
todo.txt rules. Any deviation from the same is clearly mentioned.
The deviation from common todo.txt rules is due to GNOME To Do specific
features and behaviour.

## Line Type
Currently we have general lines with task desctiption and custom
lines with GNOME To Do specific descriptions. The custom lines
start with h:1 and are intended not to be edited by the users.

  1) The task line describing a task
  2) A List line specifying names of all the lists (Starts with h:1 Lists)
  3) A Color lines storing lists color (Starts with h:1 Colors )

The format of each task line is specified below

### Task Line

indent x (Priority) Creation-Date Completion-Date title due:date note:"description" @List +parent-task
    
    :indent               - (Optional) 4 Spaces to indent a task as subtask of previous line.
    					     Don't use tabs for indenting tasks.
	:x       	          - (Optional) Marks the task as completed
	:Priority 	          - (Optional) Should appear in closed parenthesis, can be 
						     A(high), B(medium), C(low) and any other character is taken as 
						     default no priority
						     (GNOME To Do Supports 3 priority level High, Medium and low)
	:Creation-Date        - (Optional) Date on which the task was created
	:Completion-Date      - (Optional) Should be mentioned if Creation Date is mentioned
	:title                - (Required) Title describing the task
	:due:date             - (Optional) Due Date of the task
	:note:"description"   - (Optional) Long Description for the task
	:@List                - (Required) The Context of the task. Although todo.txt doesn't
						    requires a context to be mentioned but since every task in
						    GNOME To Do belongs to a list and hence it is necessary.

## Custom Lines
Custom Lines appears the end of todo.txt and starts with h:1. At the present To Do specifies
two custom lines for storing tasklists and their color.

### List Line
The list line starts with "h:1 Lists" and follows with list names. The list name start with
a "@".
i.e h:1 Lists \@Personal \@Work \@Shopping

The above example contains 3 lists or Context namely Personal, Work
and Shopping

### Color Line
The Color line describes background color for GNOME To Do list. It starts with a 
prefix h:1 Colors and color desciption appears as listname:hexcode and are separated
by space.

i.e h:1 Colors Personal:#000000 Work:#ffffff Shopping:#0000ff

In the above example the color associated with each List or Context is as:
Personal -> #000000
Work     -> #ffffff
Shopping -> #0000ff