o
    �Qeh  �                   @   s�  d dl Z d dlZd dlmZ d dlmZmZmZ d dlmZm	Z	 d dl
Z
d dlZd dlZd dlZd dlZd dlZdZdadd� Zd	d
� Zedk�r@e�dd�Zeejd �Zedkr�ed� ed� e�d� d�e�Zee� i Zejeeefd�Ze� �  ed� d�e�Z!ee!� i Z"ejee!e"fd�Z#e#� �  e#�$�  dae�d� e�d� e�$�  ed� ee"� ed� ee� edk�r$ed� ed� e�d� d�e�Zee� i Zejeeefd�Ze� �  ed� d�e�Z!ee!� i Z"ejee!e"fd�Z#e#� �  e#�$�  dae�d� e�$�  ed� ee"� ed� ee� edk�r�ed� ed � e�d!� d"�e�Zee� i Zejeeefd�Ze� �  ed� d�e�Z!ee!� i Z"ejee!e"fd�Z#e#� �  ed� d#�e�Z!ee!� i Z%ejee!e%fd�Z&e&� �  e#�$�  e&�$�  dae�d� e�$�  ed$� ee"� ed%� ee%� ed� ee� ed&k�rBed'� ed � e�d!� d�e�Zee� i Zejeeefd�Ze� �  ed� d�e�Z!ee!� i Z"ejee!e"fd�Z#e#� �  ed� d(�e�Z!ee!� i Z%ejee!e%fd�Z&e&� �  e#�$�  e&�$�  dae�d� e�$�  ed$� ee"� ed%� ee%� ed� ee� dS dS dS ))�    N)�sleep)�fcntl�F_GETFL�F_SETFL)�
O_NONBLOCK�readFTc                 C   s*  t jt�| �t jt jdd�}td� d}g }g }d }d}tr~zT|j�	� }|s(W q|�
� �� }tr4t|� t|�d�d �}	|�|	� |�d�d �d�d }
|d u rY|
}|d }n|
|krb|d }n|�||f� |
}d}|d }W n	 ty{   Y nw ts|�||f� ||d	< |d |d
< ||d< d S )NF��stdout�stderr�shellg�������?r   � ������[�   �lines�final_counter�clients)�
subprocess�Popen�shlex�split�PIPE�STDOUTr   �server_exitr	   �readline�rstrip�decode�DEBUG�print�int�append�
ValueError)�command�res�p�c�cont_l�agent_d�	agent_str�agent_c�line�contador�agent� r-   �
checkP3.py�
server_fun   sN   �



��r/   c           	      C   s�   t � � }tjt�| �tjtjdd�}d}g }	 z5|j�� }|rIt	|�dkrI|d }|�
� �� }tr5t|� |�d�d �d	�d
 }|�t|�� nW nW n	 tyU   Y nw qt � � }||d< || |d< t�|�|d< d S )NFr   r   T�   r   �=r   r   �����r   �	exec_time�mean_latency_wait)�timer   r   r   r   r   r   r	   r   �lenr   r   r   r   r    r   r!   �np�mean)	r"   r#   �
start_timer$   r%   �wait_lr*   �wait_ns�end_timer-   r-   r.   �
client_funM   s:   �
���r=   �__main__i(#  i'  r   z.## TEST 1: Server (P:reader) - Client (reader)zInit counter = 0zecho 0 > server_output.txtz$./server --port {} --priority reader)�target�argsg      �?z;./client --ip 0.0.0.0 --port {} --mode reader --threads 200zkillall serverzClient: zServer: �   z.## TEST 2 Server (P: writer) - Client (writer)zInit counter = 123zecho 123 > server_output.txtz;./client --ip 0.0.0.0 --port {} --mode writer --threads 200r0   z9## TEST 3 Server (P: writer) - 2 Clients (writer, reader)zInit counter = 200zecho 200 > server_output.txtz$./server --port {} --priority writerz:./client --ip 0.0.0.0 --port {} --mode reader --threads 10zClient (writer): zClient (reader): �   z8## TEST 4 Server (P:reader) - 2 Clients (writer, reader)z:./client --ip 0.0.0.0 --port {} --mode reader --threads 20)'r   �sysr5   r   r   r   r   �osr   r   r   �	threading�numpyr7   �randomr   r   r/   r=   �__name__�	randrange�PORTr   �argv�test_numberr   �system�format�SERVER_COMMAND�
res_server�Thread�thread_s�start�CLIENT_COMMAND�
res_client�thread_c�join�res_client2�	thread_c2r-   r-   r-   r.   �<module>   s  8
'





















 ��m